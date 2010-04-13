//
// Copyright (c) 2002-2010 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Blit.cpp: Surface copy utility class.

#include "Blit.h"

#include <d3dx9.h>

#include "main.h"
#include "common/debug.h"

namespace
{
// Standard Vertex Shader
// Input 0 is the homogenous position.
// Outputs the homogenous position as-is.
// Outputs a tex coord with (0,0) in the upper-left corner of the screen and (1,1) in the bottom right.
// C0.X must be negative half-pixel width, C0.Y must be half-pixel height. C0.ZW must be 0.
const char standardvs[] =
"struct VS_OUTPUT\n"
"{\n"
"    float4 position : POSITION;\n"
"    float4 texcoord : TEXCOORD0;\n"
"};\n"
"\n"
"uniform float4 halfPixelSize : c0;\n"
"\n"
"VS_OUTPUT main(in float4 position : POSITION)\n"
"{\n"
"    VS_OUTPUT Out;\n"
"\n"
"    Out.position = position + halfPixelSize;\n"
"    Out.texcoord = position * float4(0.5, -0.5, 1.0, 1.0) + float4(0.5, 0.5, 0, 0);\n"
"\n"
"    return Out;\n"
"}\n";

// Flip Y Vertex Shader
// Input 0 is the homogenous position.
// Outputs the homogenous position as-is.
// Outputs a tex coord with (0,1) in the upper-left corner of the screen and (1,0) in the bottom right.
// C0.XY must be the half-pixel width and height. C0.ZW must be 0.
const char flipyvs[] =
"struct VS_OUTPUT\n"
"{\n"
"    float4 position : POSITION;\n"
"    float4 texcoord : TEXCOORD0;\n"
"};\n"
"\n"
"uniform float4 halfPixelSize : c0;\n"
"\n"
"VS_OUTPUT main(in float4 position : POSITION)\n"
"{\n"
"    VS_OUTPUT Out;\n"
"\n"
"    Out.position = position + halfPixelSize;\n"
"    Out.texcoord = position * float4(0.5, 0.5, 1.0, 1.0) + float4(0.5, 0.5, 0, 0);\n"
"\n"
"    return Out;\n"
"}\n";

// Passthrough Pixel Shader
// Outputs texture 0 sampled at texcoord 0.
const char passthroughps[] =
"sampler2D tex : s0;\n"
"\n"
"float4 main(float4 texcoord : TEXCOORD0) : COLOR\n"
"{\n"
"	return tex2D(tex, texcoord.xy);\n"
"}\n";

// Luminance Conversion Pixel Shader
// Outputs sample(tex0, tc0).rrra.
// For LA output (pass A) set C0.X = 1, C0.Y = 0.
// For L output (A = 1) set C0.X = 0, C0.Y = 1.
const char luminanceps[] =
"sampler2D tex : s0;\n"
"\n"
"uniform float4 mode : c0;\n"
"\n"
"float4 main(float4 texcoord : TEXCOORD0) : COLOR\n"
"{\n"
"	float4 tmp = tex2D(tex, texcoord.xy);\n"
"	tmp.w = tmp.w * mode.x + mode.y;\n"
"	return tmp.xxxw;\n"
"}\n";

// RGB/A Component Mask Pixel Shader
// Outputs sample(tex0, tc0) with options to force RGB = 0 and/or A = 1.
// To force RGB = 0, set C0.X = 0, otherwise C0.X = 1.
// To force A = 1, set C0.Z = 0, C0.W = 1, otherwise C0.Z = 1, C0.W = 0.
const char componentmaskps[] =
"sampler2D tex : s0;\n"
"\n"
"uniform float4 mode : c0;\n"
"\n"
"float4 main(float4 texcoord : TEXCOORD0) : COLOR\n"
"{\n"
"	float4 tmp = tex2D(tex, texcoord.xy);\n"
"	tmp.xyz = tmp.xyz * mode.x;\n"
"	tmp.w = tmp.w * mode.z + mode.w;\n"
"	return tmp;\n"
"}\n";

}

namespace gl
{

const char * const Blit::mShaderSource[] =
{
    standardvs,
    flipyvs,
    passthroughps,
    luminanceps,
    componentmaskps
};

Blit::Blit(Context *context)
  : mContext(context), mQuadVertexBuffer(NULL), mQuadVertexDeclaration(NULL)
{
    initGeometry();
    memset(mCompiledShaders, 0, sizeof(mCompiledShaders));
}

Blit::~Blit()
{
    if (mQuadVertexBuffer) mQuadVertexBuffer->Release();
    if (mQuadVertexDeclaration) mQuadVertexDeclaration->Release();

    for (int i = 0; i < SHADER_COUNT; i++)
    {
        if (mCompiledShaders[i])
        {
            mCompiledShaders[i]->Release();
        }
    }
}

void Blit::initGeometry()
{
    static const float quad[] =
    {
        -1, -1,
        -1,  1,
         1, -1,
         1,  1
    };

    IDirect3DDevice9 *device = getDevice();

    HRESULT hr = device->CreateVertexBuffer(sizeof(quad), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &mQuadVertexBuffer, NULL);

    if (FAILED(hr))
    {
        ASSERT(hr == D3DERR_OUTOFVIDEOMEMORY || hr == E_OUTOFMEMORY);
        return error(GL_OUT_OF_MEMORY);
    }

    void *lockPtr;
    mQuadVertexBuffer->Lock(0, 0, &lockPtr, 0);
    memcpy(lockPtr, quad, sizeof(quad));
    mQuadVertexBuffer->Unlock();

    static const D3DVERTEXELEMENT9 elements[] =
    {
        { 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        D3DDECL_END()
    };

    hr = device->CreateVertexDeclaration(elements, &mQuadVertexDeclaration);
    if (FAILED(hr))
    {
        ASSERT(hr == D3DERR_OUTOFVIDEOMEMORY || hr == E_OUTOFMEMORY);
        return error(GL_OUT_OF_MEMORY);
    }
}

template <class D3DShaderType>
bool Blit::setShader(ShaderId source, const char *profile,
                     HRESULT (WINAPI IDirect3DDevice9::*createShader)(const DWORD *, D3DShaderType**),
                     HRESULT (WINAPI IDirect3DDevice9::*setShader)(D3DShaderType*))
{
    IDirect3DDevice9 *device = getDevice();

    D3DShaderType *shader;

    if (mCompiledShaders[source] != NULL)
    {
        shader = static_cast<D3DShaderType*>(mCompiledShaders[source]);
    }
    else
    {
        ID3DXBuffer *shaderCode;
        HRESULT hr = D3DXCompileShader(mShaderSource[source], strlen(mShaderSource[source]), NULL, NULL, "main", profile, 0, &shaderCode, NULL, NULL);

        if (FAILED(hr))
        {
            ERR("Failed to compile %s shader for blit operation %d, error 0x%08X.", profile, (int)source, hr);
            return false;
        }

        hr = (device->*createShader)(static_cast<const DWORD*>(shaderCode->GetBufferPointer()), &shader);
        if (FAILED(hr))
        {
            shaderCode->Release();
            ERR("Failed to create %s shader for blit operation %d, error 0x%08X.", profile, (int)source, hr);
            return false;
        }

        shaderCode->Release();

        mCompiledShaders[source] = shader;
    }

    HRESULT hr = (device->*setShader)(shader);

    if (FAILED(hr))
    {
        ERR("Failed to set %s shader for blit operation %d, error 0x%08X.", profile, (int)source, hr);
        return false;
    }

    return true;
}

bool Blit::setVertexShader(ShaderId shader)
{
    return setShader<IDirect3DVertexShader9>(shader, mContext->getVertexShaderProfile(), &IDirect3DDevice9::CreateVertexShader, &IDirect3DDevice9::SetVertexShader);
}

bool Blit::setPixelShader(ShaderId shader)
{
    return setShader<IDirect3DPixelShader9>(shader, mContext->getPixelShaderProfile(), &IDirect3DDevice9::CreatePixelShader, &IDirect3DDevice9::SetPixelShader);
}

bool Blit::formatConvert(IDirect3DSurface9 *source, const RECT &sourceRect, GLenum destFormat, GLint xoffset, GLint yoffset, IDirect3DSurface9 *dest)
{
    IDirect3DTexture9 *texture = copySurfaceToTexture(source, sourceRect);
    if (!texture)
    {
        return false;
    }

    IDirect3DDevice9 *device = getDevice();

    device->SetTexture(0, texture);
    device->SetRenderTarget(0, dest);

    setViewport(sourceRect, xoffset, yoffset, dest);

    setCommonBlitState();
    if (setFormatConvertShaders(destFormat))
    {
        render();
    }

    texture->Release();

    return true;
}

bool Blit::setFormatConvertShaders(GLenum destFormat)
{
    bool okay = setVertexShader(SHADER_VS_STANDARD);

    switch (destFormat)
    {
      default: UNREACHABLE();
      case GL_RGBA:
      case GL_RGB:
      case GL_ALPHA:
        okay = okay && setPixelShader(SHADER_PS_COMPONENTMASK);
        break;

      case GL_LUMINANCE:
      case GL_LUMINANCE_ALPHA:
        okay = okay && setPixelShader(SHADER_PS_LUMINANCE);
        break;
    }

    if (!okay)
    {
        return false;
    }

    enum { X = 0, Y = 1, Z = 2, W = 3 };

    // The meaning of this constant depends on the shader that was selected.
    // See the shader assembly code above for details.
    float psConst0[4] = { 0, 0, 0, 0 };

    switch (destFormat)
    {
      default: UNREACHABLE();
      case GL_RGBA:
        psConst0[X] = 1;
        psConst0[Z] = 1;
        break;

      case GL_RGB:
        psConst0[X] = 1;
        psConst0[W] = 1;
        break;

      case GL_ALPHA:
        psConst0[Z] = 1;
        break;

      case GL_LUMINANCE:
        psConst0[Y] = 1;
        break;

      case GL_LUMINANCE_ALPHA:
        psConst0[X] = 1;
        break;
    }

    getDevice()->SetPixelShaderConstantF(0, psConst0, 1);

    return true;
}

IDirect3DTexture9 *Blit::copySurfaceToTexture(IDirect3DSurface9 *surface, const RECT &sourceRect)
{
    IDirect3DDevice9 *device = getDevice();

    D3DSURFACE_DESC sourceDesc;
    surface->GetDesc(&sourceDesc);

    // Copy the render target into a texture
    IDirect3DTexture9 *texture;
    HRESULT result = device->CreateTexture(sourceRect.right - sourceRect.left, sourceRect.top - sourceRect.bottom, 1, D3DUSAGE_RENDERTARGET, sourceDesc.Format, D3DPOOL_DEFAULT, &texture, NULL);

    if (FAILED(result))
    {
        ASSERT(result == D3DERR_OUTOFVIDEOMEMORY || result == E_OUTOFMEMORY);
        return error(GL_OUT_OF_MEMORY, (IDirect3DTexture9*)NULL);
    }

    IDirect3DSurface9 *textureSurface;
    result = texture->GetSurfaceLevel(0, &textureSurface);

    if (FAILED(result))
    {
        ASSERT(result == D3DERR_OUTOFVIDEOMEMORY || result == E_OUTOFMEMORY);
        texture->Release();
        return error(GL_OUT_OF_MEMORY, (IDirect3DTexture9*)NULL);
    }

    RECT d3dSourceRect;
    d3dSourceRect.left = sourceRect.left;
    d3dSourceRect.right = sourceRect.right;
    d3dSourceRect.top = sourceRect.bottom;
    d3dSourceRect.bottom = sourceRect.top;

    result = device->StretchRect(surface, &d3dSourceRect, textureSurface, NULL, D3DTEXF_NONE);

    textureSurface->Release();

    if (FAILED(result))
    {
        ASSERT(result == D3DERR_OUTOFVIDEOMEMORY || result == E_OUTOFMEMORY);
        texture->Release();
        return error(GL_OUT_OF_MEMORY, (IDirect3DTexture9*)NULL);
    }

    return texture;
}

void Blit::setViewport(const RECT &sourceRect, GLint xoffset, GLint yoffset, IDirect3DSurface9 *dest)
{
    D3DSURFACE_DESC desc;
    dest->GetDesc(&desc);

    IDirect3DDevice9 *device = getDevice();

    D3DVIEWPORT9 vp;
    vp.X      = xoffset;
    vp.Y      = yoffset;
    vp.Width  = sourceRect.right - sourceRect.left;
    vp.Height = sourceRect.top - sourceRect.bottom;
    vp.MinZ   = 0.0f;
    vp.MaxZ   = 1.0f;
    device->SetViewport(&vp);

    float halfPixelAdjust[4] = { -1.0f/vp.Width, 1.0f/vp.Height, 0, 0 };
    device->SetVertexShaderConstantF(0, halfPixelAdjust, 1);
}

void Blit::setCommonBlitState()
{
    IDirect3DDevice9 *device = getDevice();

    device->SetDepthStencilSurface(NULL);

    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_RED);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
}

void Blit::render()
{
    IDirect3DDevice9 *device = getDevice();

    HRESULT hr = device->SetStreamSource(0, mQuadVertexBuffer, 0, 2 * sizeof(float));
    hr = device->SetVertexDeclaration(mQuadVertexDeclaration);

    device->BeginScene();
    hr = device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
    device->EndScene();
}

}
