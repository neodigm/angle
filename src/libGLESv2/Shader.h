//
// Copyright (c) 2002-2013 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Shader.h: Defines the abstract gl::Shader class and its concrete derived
// classes VertexShader and FragmentShader. Implements GL shader objects and
// related functionality. [OpenGL ES 2.0.24] section 2.10 page 24 and section
// 3.8 page 84.

#ifndef LIBGLESV2_SHADER_H_
#define LIBGLESV2_SHADER_H_


#include <string>
#include <list>
#include <vector>

#include "angle_gl.h"
#include <GLSLANG/ShaderLang.h>

#include "common/angleutils.h"
#include "libGLESv2/angletypes.h"

namespace rx
{
class ShaderImpl;
}

namespace gl
{
class ResourceManager;

struct PackedVarying : public sh::Varying
{
    unsigned int registerIndex; // Assigned during link

    PackedVarying(const sh::Varying &varying)
      : sh::Varying(varying),
        registerIndex(GL_INVALID_INDEX)
    {}

    bool registerAssigned() const { return registerIndex != GL_INVALID_INDEX; }

    void resetRegisterAssignment()
    {
        registerIndex = GL_INVALID_INDEX;
    }
};

class Shader
{
  public:
    Shader(ResourceManager *manager, rx::ShaderImpl *impl, GLenum type, GLuint handle);

    virtual ~Shader();

    GLenum getType() const { return mType; }
    GLuint getHandle() const;

    rx::ShaderImpl *getImplementation() { return mShader; }
    const rx::ShaderImpl *getImplementation() const { return mShader; }

    void deleteSource();
    void setSource(GLsizei count, const char *const *string, const GLint *length);
    int getInfoLogLength() const;
    void getInfoLog(GLsizei bufSize, GLsizei *length, char *infoLog) const;
    int getSourceLength() const;
    void getSource(GLsizei bufSize, GLsizei *length, char *buffer) const;
    int getTranslatedSourceLength() const;
    void getTranslatedSource(GLsizei bufSize, GLsizei *length, char *buffer) const;

    void compile();
    bool isCompiled() const { return mCompiled; }

    void addRef();
    void release();
    unsigned int getRefCount() const;
    bool isFlaggedForDeletion() const;
    void flagForDeletion();

  private:
    DISALLOW_COPY_AND_ASSIGN(Shader);

    static void getSourceImpl(const std::string &source, GLsizei bufSize, GLsizei *length, char *buffer);

    rx::ShaderImpl *mShader;
    const GLuint mHandle;
    const GLenum mType;
    std::string mSource;
    unsigned int mRefCount;     // Number of program objects this shader is attached to
    bool mDeleteStatus;         // Flag to indicate that the shader can be deleted when no longer in use
    bool mCompiled;             // Indicates if this shader has been successfully compiled

    ResourceManager *mResourceManager;
};

// TODO: These are now stubs. We should remove them and use Shader exclusively.
class VertexShader : public Shader
{
  public:
    VertexShader(ResourceManager *manager, rx::ShaderImpl *impl, GLuint handle);

    ~VertexShader();

  private:
    DISALLOW_COPY_AND_ASSIGN(VertexShader);
};

class FragmentShader : public Shader
{
  public:
    FragmentShader(ResourceManager *manager, rx::ShaderImpl *impl, GLuint handle);

    ~FragmentShader();

  private:
    DISALLOW_COPY_AND_ASSIGN(FragmentShader);
};
}

#endif   // LIBGLESV2_SHADER_H_
