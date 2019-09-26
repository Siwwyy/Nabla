// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in Irrlicht.h

#ifndef __C_VIDEO_OPEN_GL_H_INCLUDED__
#define __C_VIDEO_OPEN_GL_H_INCLUDED__

#include "irr/core/core.h"

#include "SIrrCreationParameters.h"

namespace irr
{
	class CIrrDeviceWin32;
	class CIrrDeviceLinux;
	class CIrrDeviceSDL;
	class CIrrDeviceMacOSX;
}

#ifdef _IRR_COMPILE_WITH_OPENGL_

#include "CNullDriver.h"
// also includes the OpenGL stuff
#include "COpenGLExtensionHandler.h"
#include "COpenGLDriverFence.h"
#include "COpenGLTransformFeedback.h"
#include "COpenCLHandler.h"
#include "irr/video/COpenGLSpecializedShader.h"
#include "irr/video/COpenGLRenderpassIndependentPipeline.h"
#include "irr/video/COpenGLDescriptorSet.h"
#include "irr/video/COpenGLPipelineLayout.h"

#include <map>
#include "FW_Mutex.h"

namespace irr
{
namespace asset
{
    class IGLSLCompiler;
}

namespace video
{
	class COpenGLTexture;
	class COpenGLFrameBuffer;

    enum GL_STATE_BITS : uint32_t
    {
        // has to be flushed before constants are pushed (before `extGlProgramUniform*`)
        GSB_PIPELINE = 0x1u,
        // (all stancil, depth, etc state) change just before draw
        GSB_RASTER_PARAMETERS = 0x2u,
        // we want the two to happen together and just before a draw (set VAO first, then binding)
        GSB_VAO_AND_VERTEX_INPUT = 0x4u,
        // flush just before (indirect)dispatch or (multi)(indirect)draw, textures and samplers first, then storage image, then SSBO, finally UBO
        GSB_DESCRIPTOR_SETS = 0x8u,
        // flush everything
        GSB_ALL = ~0x0u
    };
    struct SOpenGLState
    {
        struct SVAO {
            GLuint GLname;
            uint64_t lastValidated;
        };
        typedef std::pair<COpenGLRenderpassIndependentPipeline::SVAOHash, SVAO> HashVAOPair;

        core::smart_refctd_ptr<const COpenGLRenderpassIndependentPipeline> pipeline;

        struct {
            //in GL it is possible to set polygon mode separately for back- and front-faces, but in VK it's one setting for both
            GLenum polygonMode = GL_FILL;
            GLenum faceCullingEnable = 0;
            GLenum cullFace = GL_BACK;
            //in VK stencil params (both: stencilOp and stencilFunc) are 2 distinct for back- and front-faces, but in GL it's one for both
            struct SStencilOp {
                GLenum sfail = GL_KEEP;
                GLenum dpfail = GL_KEEP;
                GLenum dppass = GL_KEEP;
                bool operator!=(const SStencilOp& rhs) const { return sfail!=rhs.sfail || dpfail!=rhs.dpfail || dppass!=rhs.dppass; }
            };
            SStencilOp stencilOp_front, stencilOp_back;
            struct SStencilFunc {
                GLenum func = GL_ALWAYS;
                GLint ref = 0;
                GLuint mask = ~static_cast<GLuint>(0u);
                bool operator!=(const SStencilFunc& rhs) const { return func!=rhs.func || ref!=rhs.ref || mask!=rhs.mask; }
            };
            SStencilFunc stencilFunc_front, stencilFunc_back;
            GLenum depthFunc = GL_LESS;
            GLenum frontFace = GL_CCW;
            GLboolean depthClampEnable = 0;
            GLboolean rasterizerDiscardEnable = 0;
            GLboolean polygonOffsetEnable = 0;
            struct SPolyOffset {
                GLfloat factor = 0.f;//depthBiasSlopeFactor 
                GLfloat units = 0.f;//depthBiasConstantFactor 
                bool operator!=(const SPolyOffset& rhs) const { return factor!=rhs.factor || units!=rhs.units; }
            } polygonOffset;
            GLfloat lineWidth = 1.f;
            GLboolean sampleShadingEnable = 0;
            GLfloat minSampleShading = 0.f;
            GLboolean sampleMaskEnable = 0;
            GLbitfield sampleMask[2]{~static_cast<GLbitfield>(0), ~static_cast<GLbitfield>(0)};
            GLboolean sampleAlphaToCoverageEnable = 0;
            GLboolean sampleAlphaToOneEnable = 0;
            GLboolean depthTestEnable = 0;
            GLboolean depthWriteEnable = 1;
            //GLboolean depthBoundsTestEnable;
            GLboolean stencilTestEnable = 0;
            GLboolean multisampleEnable = 1;
            struct {
                GLenum origin;
                GLenum depth;
            } clipControl;
            struct {
                GLclampd znear;
                GLclampd zfar;
            } depthRange;

            GLboolean logicOpEnable = 0;
            GLenum logicOp = GL_COPY;
            struct SDrawbufferBlending
            {
                GLboolean blendEnable = 0;
                struct SBlendFunc {
                    GLenum srcRGB = GL_ONE;
                    GLenum dstRGB = GL_ZERO;
                    GLenum srcAlpha = GL_ONE;
                    GLenum dstAlpha = GL_ZERO;
                    bool operator!=(const SBlendFunc& rhs) const { return srcRGB!=rhs.srcRGB || dstRGB!=rhs.dstRGB || srcAlpha!=rhs.srcAlpha || dstAlpha!=rhs.dstAlpha; }
                } blendFunc;
                struct SBlendEq {
                    GLenum modeRGB = GL_FUNC_ADD;
                    GLenum modeAlpha = GL_FUNC_ADD;
                    bool operator!=(const SBlendEq& rhs) const { return modeRGB!=rhs.modeRGB || modeAlpha!=rhs.modeAlpha; }
                } blendEquation;
                struct SColorWritemask {
                    GLboolean colorWritemask[4]{ 1,1,1,1 };
                    bool operator!=(const SColorWritemask& rhs) const { return memcmp(colorWritemask, rhs.colorWritemask, 4); }
                } colorMask;
            } drawbufferBlend[asset::SBlendParams::MAX_COLOR_ATTACHMENT_COUNT];
        } rasterParams;

        struct {
            HashVAOPair vao;
            struct SBnd {
                core::smart_refctd_ptr<const COpenGLBuffer> buf = 0;
                GLintptr offset = 0;
                bool operator!=(const SBnd& rhs) const { return buf!=rhs.buf || offset!=rhs.offset; }
            } bindings[16];
            core::smart_refctd_ptr<const COpenGLBuffer> indexBuf;

            //putting it here because idk where else
            core::smart_refctd_ptr<const COpenGLBuffer> indirectDrawBuf;
        } vertexInputParams;

        struct {
            core::smart_refctd_ptr<const COpenGLDescriptorSet> descSets[video::IGPUPipelineLayout::DESCRIPTOR_SET_COUNT];
        } descriptorsParams;
    };

	class COpenGLDriver : public CNullDriver, public COpenGLExtensionHandler
	{
    protected:
		//! destructor
		virtual ~COpenGLDriver();

	public:
        struct SAuxContext;

		#ifdef _IRR_COMPILE_WITH_WINDOWS_DEVICE_
		COpenGLDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io, CIrrDeviceWin32* device, const asset::IGLSLCompiler* glslcomp);
		//! inits the windows specific parts of the open gl driver
		bool initDriver(CIrrDeviceWin32* device);
		bool changeRenderContext(const SExposedVideoData& videoData, CIrrDeviceWin32* device);
		#endif

		#ifdef _IRR_COMPILE_WITH_X11_DEVICE_
		COpenGLDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io, CIrrDeviceLinux* device, asset::IGLSLCompiler* glslcomp);
		//! inits the GLX specific parts of the open gl driver
		bool initDriver(CIrrDeviceLinux* device, SAuxContext* auxCtxts);
		bool changeRenderContext(const SExposedVideoData& videoData, CIrrDeviceLinux* device);
		#endif

		#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
		COpenGLDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io, CIrrDeviceSDL* device, asset::IGLSLCompiler* glslcomp);
		#endif

		#ifdef _IRR_COMPILE_WITH_OSX_DEVICE_
		COpenGLDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io, CIrrDeviceMacOSX *device, asset::IGLSLCompiler* glslcomp);
		#endif

        inline virtual bool isAllowedVertexAttribFormat(asset::E_FORMAT _fmt) const override
        {
            using namespace asset;
            switch (_fmt)
            {
            // signed/unsigned byte
            case EF_R8_UNORM:
            case EF_R8_SNORM:
            case EF_R8_UINT:
            case EF_R8_SINT:
            case EF_R8G8_UNORM:
            case EF_R8G8_SNORM:
            case EF_R8G8_UINT:
            case EF_R8G8_SINT:
            case EF_R8G8B8_UNORM:
            case EF_R8G8B8_SNORM:
            case EF_R8G8B8_UINT:
            case EF_R8G8B8_SINT:
            case EF_R8G8B8A8_UNORM:
            case EF_R8G8B8A8_SNORM:
            case EF_R8G8B8A8_UINT:
            case EF_R8G8B8A8_SINT:
            case EF_R8_USCALED:
            case EF_R8_SSCALED:
            case EF_R8G8_USCALED:
            case EF_R8G8_SSCALED:
            case EF_R8G8B8_USCALED:
            case EF_R8G8B8_SSCALED:
            case EF_R8G8B8A8_USCALED:
            case EF_R8G8B8A8_SSCALED:
            // unsigned byte BGRA (normalized only)
            case EF_B8G8R8A8_UNORM:
            // unsigned/signed short
            case EF_R16_UNORM:
            case EF_R16_SNORM:
            case EF_R16_UINT:
            case EF_R16_SINT:
            case EF_R16G16_UNORM:
            case EF_R16G16_SNORM:
            case EF_R16G16_UINT:
            case EF_R16G16_SINT:
            case EF_R16G16B16_UNORM:
            case EF_R16G16B16_SNORM:
            case EF_R16G16B16_UINT:
            case EF_R16G16B16_SINT:
            case EF_R16G16B16A16_UNORM:
            case EF_R16G16B16A16_SNORM:
            case EF_R16G16B16A16_UINT:
            case EF_R16G16B16A16_SINT:
            case EF_R16_USCALED:
            case EF_R16_SSCALED:
            case EF_R16G16_USCALED:
            case EF_R16G16_SSCALED:
            case EF_R16G16B16_USCALED:
            case EF_R16G16B16_SSCALED:
            case EF_R16G16B16A16_USCALED:
            case EF_R16G16B16A16_SSCALED:
            // unsigned/signed int
            case EF_R32_UINT:
            case EF_R32_SINT:
            case EF_R32G32_UINT:
            case EF_R32G32_SINT:
            case EF_R32G32B32_UINT:
            case EF_R32G32B32_SINT:
            case EF_R32G32B32A32_UINT:
            case EF_R32G32B32A32_SINT:
            // unsigned/signed rgb10a2 BGRA (normalized only)
            case EF_A2R10G10B10_UNORM_PACK32:
            case EF_A2R10G10B10_SNORM_PACK32:
            // unsigned/signed rgb10a2
            case EF_A2B10G10R10_UNORM_PACK32:
            case EF_A2B10G10R10_SNORM_PACK32:
            case EF_A2B10G10R10_UINT_PACK32:
            case EF_A2B10G10R10_SINT_PACK32:
            case EF_A2B10G10R10_SSCALED_PACK32:
            case EF_A2B10G10R10_USCALED_PACK32:
            // GL_UNSIGNED_INT_10F_11F_11F_REV
            case EF_B10G11R11_UFLOAT_PACK32:
            // half float
            case EF_R16_SFLOAT:
            case EF_R16G16_SFLOAT:
            case EF_R16G16B16_SFLOAT:
            case EF_R16G16B16A16_SFLOAT:
            // float
            case EF_R32_SFLOAT:
            case EF_R32G32_SFLOAT:
            case EF_R32G32B32_SFLOAT:
            case EF_R32G32B32A32_SFLOAT:
            // double
            case EF_R64_SFLOAT:
            case EF_R64G64_SFLOAT:
            case EF_R64G64B64_SFLOAT:
            case EF_R64G64B64A64_SFLOAT:
                return true;
            default: return false;
            }
        }
        inline virtual bool isColorRenderableFormat(asset::E_FORMAT _fmt) const override
        {
            using namespace asset;
            switch (_fmt)
            {
            case EF_A1R5G5B5_UNORM_PACK16:
            case EF_B5G6R5_UNORM_PACK16:
            case EF_R5G6B5_UNORM_PACK16:
            case EF_R4G4_UNORM_PACK8:
            case EF_R4G4B4A4_UNORM_PACK16:
            case EF_B4G4R4A4_UNORM_PACK16:
            case EF_R8_UNORM:
            case EF_R8_SNORM:
            case EF_R8_UINT:
            case EF_R8_SINT:
            case EF_R8G8_UNORM:
            case EF_R8G8_SNORM:
            case EF_R8G8_UINT:
            case EF_R8G8_SINT:
            case EF_R8G8B8_UNORM:
            case EF_R8G8B8_SNORM:
            case EF_R8G8B8_UINT:
            case EF_R8G8B8_SINT:
            case EF_R8G8B8_SRGB:
            case EF_R8G8B8A8_UNORM:
            case EF_R8G8B8A8_SNORM:
            case EF_R8G8B8A8_UINT:
            case EF_R8G8B8A8_SINT:
            case EF_R8G8B8A8_SRGB:
            case EF_A8B8G8R8_UNORM_PACK32:
            case EF_A8B8G8R8_SNORM_PACK32:
            case EF_A8B8G8R8_UINT_PACK32:
            case EF_A8B8G8R8_SINT_PACK32:
            case EF_A8B8G8R8_SRGB_PACK32:
            case EF_A2B10G10R10_UNORM_PACK32:
            case EF_A2B10G10R10_UINT_PACK32:
            case EF_R16_UNORM:
            case EF_R16_SNORM:
            case EF_R16_UINT:
            case EF_R16_SINT:
            case EF_R16_SFLOAT:
            case EF_R16G16_UNORM:
            case EF_R16G16_SNORM:
            case EF_R16G16_UINT:
            case EF_R16G16_SINT:
            case EF_R16G16_SFLOAT:
            case EF_R16G16B16_UNORM:
            case EF_R16G16B16_SNORM:
            case EF_R16G16B16_UINT:
            case EF_R16G16B16_SINT:
            case EF_R16G16B16_SFLOAT:
            case EF_R16G16B16A16_UNORM:
            case EF_R16G16B16A16_SNORM:
            case EF_R16G16B16A16_UINT:
            case EF_R16G16B16A16_SINT:
            case EF_R16G16B16A16_SFLOAT:
            case EF_R32_UINT:
            case EF_R32_SINT:
            case EF_R32_SFLOAT:
            case EF_R32G32_UINT:
            case EF_R32G32_SINT:
            case EF_R32G32_SFLOAT:
            case EF_R32G32B32_UINT:
            case EF_R32G32B32_SINT:
            case EF_R32G32B32_SFLOAT:
            case EF_R32G32B32A32_UINT:
            case EF_R32G32B32A32_SINT:
            case EF_R32G32B32A32_SFLOAT:
                return true;
            default:
            {
                GLint res = GL_FALSE;
                extGlGetInternalformativ(GL_TEXTURE_2D, COpenGLTexture::getOpenGLFormatAndParametersFromColorFormat(_fmt), GL_COLOR_RENDERABLE, sizeof(res), &res);
                return res==GL_TRUE;
            }
            }
        }
        inline virtual bool isAllowedImageStoreFormat(asset::E_FORMAT _fmt) const override
        {
            using namespace asset;
            switch (_fmt)
            {
            case EF_R32G32B32A32_SFLOAT:
            case EF_R16G16B16A16_SFLOAT:
            case EF_R32G32_SFLOAT:
            case EF_R16G16_SFLOAT:
            case EF_B10G11R11_UFLOAT_PACK32:
            case EF_R32_SFLOAT:
            case EF_R16_SFLOAT:
            case EF_R16G16B16A16_UNORM:
            case EF_A2B10G10R10_UNORM_PACK32:
            case EF_R8G8B8A8_UNORM:
            case EF_R16G16_UNORM:
            case EF_R8G8_UNORM:
            case EF_R16_UNORM:
            case EF_R8_UNORM:
            case EF_R16G16B16A16_SNORM:
            case EF_R8G8B8A8_SNORM:
            case EF_R16G16_SNORM:
            case EF_R8G8_SNORM:
            case EF_R16_SNORM:
            case EF_R32G32B32A32_UINT:
            case EF_R16G16B16A16_UINT:
            case EF_A2B10G10R10_UINT_PACK32:
            case EF_R8G8B8A8_UINT:
            case EF_R32G32_UINT:
            case EF_R16G16_UINT:
            case EF_R8G8_UINT:
            case EF_R32_UINT:
            case EF_R16_UINT:
            case EF_R8_UINT:
            case EF_R32G32B32A32_SINT:
            case EF_R16G16B16A16_SINT:
            case EF_R8G8B8A8_SINT:
            case EF_R32G32_SINT:
            case EF_R16G16_SINT:
            case EF_R8G8_SINT:
            case EF_R32_SINT:
            case EF_R16_SINT:
            case EF_R8_SINT:
                return true;
            default: return false;
            }
        }
        inline virtual bool isAllowedTextureFormat(asset::E_FORMAT _fmt) const override
        {
            using namespace asset;
            // opengl spec section 8.5.1
            switch (_fmt)
            {
            // formats checked as "Req. tex"
            case EF_R8_UNORM:
            case EF_R8_SNORM:
            case EF_R16_UNORM:
            case EF_R16_SNORM:
            case EF_R8G8_UNORM:
            case EF_R8G8_SNORM:
            case EF_R16G16_UNORM:
            case EF_R16G16_SNORM:
            case EF_R8G8B8_UNORM:
            case EF_R8G8B8_SNORM:
            case EF_A1R5G5B5_UNORM_PACK16:
            case EF_R8G8B8A8_SRGB:
            case EF_A8B8G8R8_UNORM_PACK32:
            case EF_A8B8G8R8_SNORM_PACK32:
            case EF_A8B8G8R8_SRGB_PACK32:
            case EF_R16_SFLOAT:
            case EF_R16G16_SFLOAT:
            case EF_R16G16B16_SFLOAT:
            case EF_R16G16B16A16_SFLOAT:
            case EF_R32_SFLOAT:
            case EF_R32G32_SFLOAT:
            case EF_R32G32B32_SFLOAT:
            case EF_R32G32B32A32_SFLOAT:
            case EF_B10G11R11_UFLOAT_PACK32:
            case EF_E5B9G9R9_UFLOAT_PACK32:
            case EF_A2B10G10R10_UNORM_PACK32:
            case EF_A2B10G10R10_UINT_PACK32:
            case EF_R16G16B16A16_UNORM:
            case EF_R8_UINT:
            case EF_R8_SINT:
            case EF_R8G8_UINT:
            case EF_R8G8_SINT:
            case EF_R8G8B8_UINT:
            case EF_R8G8B8_SINT:
            case EF_R8G8B8A8_UNORM:
            case EF_R8G8B8A8_SNORM:
            case EF_R8G8B8A8_UINT:
            case EF_R8G8B8A8_SINT:
            case EF_B8G8R8A8_UINT:
            case EF_R16_UINT:
            case EF_R16_SINT:
            case EF_R16G16_UINT:
            case EF_R16G16_SINT:
            case EF_R16G16B16_UINT:
            case EF_R16G16B16_SINT:
            case EF_R16G16B16A16_UINT:
            case EF_R16G16B16A16_SINT:
            case EF_R32_UINT:
            case EF_R32_SINT:
            case EF_R32G32_UINT:
            case EF_R32G32_SINT:
            case EF_R32G32B32_UINT:
            case EF_R32G32B32_SINT:
            case EF_R32G32B32A32_UINT:
            case EF_R32G32B32A32_SINT:

            // depth/stencil/depth+stencil formats checked as "Req. format"
            case EF_D16_UNORM:
            case EF_X8_D24_UNORM_PACK32:
            case EF_D32_SFLOAT:
            case EF_D24_UNORM_S8_UINT:
            case EF_S8_UINT:

            // specific compressed formats
            case EF_BC6H_UFLOAT_BLOCK:
            case EF_BC6H_SFLOAT_BLOCK:
            case EF_BC7_UNORM_BLOCK:
            case EF_BC7_SRGB_BLOCK:
            case EF_ETC2_R8G8B8_UNORM_BLOCK:
            case EF_ETC2_R8G8B8_SRGB_BLOCK:
            case EF_ETC2_R8G8B8A1_UNORM_BLOCK:
            case EF_ETC2_R8G8B8A1_SRGB_BLOCK:
            case EF_ETC2_R8G8B8A8_UNORM_BLOCK:
            case EF_ETC2_R8G8B8A8_SRGB_BLOCK:
            case EF_EAC_R11_UNORM_BLOCK:
            case EF_EAC_R11_SNORM_BLOCK:
            case EF_EAC_R11G11_UNORM_BLOCK:
            case EF_EAC_R11G11_SNORM_BLOCK:
                return true;

            // astc
            case EF_ASTC_4x4_UNORM_BLOCK:
            case EF_ASTC_5x4_UNORM_BLOCK:
            case EF_ASTC_5x5_UNORM_BLOCK:
            case EF_ASTC_6x5_UNORM_BLOCK:
            case EF_ASTC_6x6_UNORM_BLOCK:
            case EF_ASTC_8x5_UNORM_BLOCK:
            case EF_ASTC_8x6_UNORM_BLOCK:
            case EF_ASTC_8x8_UNORM_BLOCK:
            case EF_ASTC_10x5_UNORM_BLOCK:
            case EF_ASTC_10x6_UNORM_BLOCK:
            case EF_ASTC_10x8_UNORM_BLOCK:
            case EF_ASTC_10x10_UNORM_BLOCK:
            case EF_ASTC_12x10_UNORM_BLOCK:
            case EF_ASTC_12x12_UNORM_BLOCK:
            case EF_ASTC_4x4_SRGB_BLOCK:
            case EF_ASTC_5x4_SRGB_BLOCK:
            case EF_ASTC_5x5_SRGB_BLOCK:
            case EF_ASTC_6x5_SRGB_BLOCK:
            case EF_ASTC_6x6_SRGB_BLOCK:
            case EF_ASTC_8x5_SRGB_BLOCK:
            case EF_ASTC_8x6_SRGB_BLOCK:
            case EF_ASTC_8x8_SRGB_BLOCK:
            case EF_ASTC_10x5_SRGB_BLOCK:
            case EF_ASTC_10x6_SRGB_BLOCK:
            case EF_ASTC_10x8_SRGB_BLOCK:
            case EF_ASTC_10x10_SRGB_BLOCK:
            case EF_ASTC_12x10_SRGB_BLOCK:
            case EF_ASTC_12x12_SRGB_BLOCK:
                return queryOpenGLFeature(IRR_KHR_texture_compression_astc_ldr);

            default: return false;
            }
        }
        inline virtual bool isHardwareBlendableFormat(asset::E_FORMAT _fmt) const override
        {
            return isColorRenderableFormat(_fmt) && (asset::isNormalizedFormat(_fmt) || asset::isFloatingPointFormat(_fmt));
        }

        core::smart_refctd_ptr<IGPUShader> createGPUShader(const asset::ICPUShader* _cpushader) override;
        core::smart_refctd_ptr<IGPUSpecializedShader> createGPUSpecializedShader(const IGPUShader* _unspecialized, const asset::ISpecializationInfo* _specInfo) override;

		//! generic version which overloads the unimplemented versions
		bool changeRenderContext(const SExposedVideoData& videoData, void* device) {return false;}

        bool initAuxContext();
        const SAuxContext* getThreadContext(const std::thread::id& tid=std::this_thread::get_id()) const;
        bool deinitAuxContext();

        virtual uint16_t retrieveDisplayRefreshRate() const override final;

		virtual IGPUBuffer* createGPUBufferOnDedMem(const IDriverMemoryBacked::SDriverMemoryRequirements& initialMreqs, const bool canModifySubData = false) override final;

        void flushMappedMemoryRanges(uint32_t memoryRangeCount, const video::IDriverMemoryAllocation::MappedMemoryRange* pMemoryRanges) override final;

        void invalidateMappedMemoryRanges(uint32_t memoryRangeCount, const video::IDriverMemoryAllocation::MappedMemoryRange* pMemoryRanges) override final;

        void copyBuffer(IGPUBuffer* readBuffer, IGPUBuffer* writeBuffer, size_t readOffset, size_t writeOffset, size_t length) override final;

		//! clears the zbuffer
		virtual bool beginScene(bool backBuffer=true, bool zBuffer=true,
				SColor color=SColor(255,0,0,0),
				const SExposedVideoData& videoData=SExposedVideoData(),
				core::rect<int32_t>* sourceRect=0);

		//! presents the rendered scene on the screen, returns false if failed
		virtual bool endScene();


		virtual void beginQuery(IQueryObject* query);
		virtual void endQuery(IQueryObject* query);
		virtual void beginQuery(IQueryObject* query, const size_t& index);
		virtual void endQuery(IQueryObject* query, const size_t& index);

        virtual IQueryObject* createPrimitivesGeneratedQuery();
        virtual IQueryObject* createXFormFeedbackPrimitiveQuery();
        virtual IQueryObject* createElapsedTimeQuery();
        virtual IGPUTimestampQuery* createTimestampQuery();


        virtual void drawMeshBuffer(const video::IGPUMeshBuffer* mb);

		virtual void drawArraysIndirect(const asset::IMeshDataFormatDesc<video::IGPUBuffer>* vao,
                                        const asset::E_PRIMITIVE_TYPE& mode,
                                        const IGPUBuffer* indirectDrawBuff,
                                        const size_t& offset, const size_t& count, const size_t& stride);
		virtual void drawIndexedIndirect(const asset::IMeshDataFormatDesc<video::IGPUBuffer>* vao,
                                            const asset::E_PRIMITIVE_TYPE& mode,
                                            const asset::E_INDEX_TYPE& type, const IGPUBuffer* indirectDrawBuff,
                                            const size_t& offset, const size_t& count, const size_t& stride);


		//! queries the features of the driver, returns true if feature is available
		virtual bool queryFeature(const E_DRIVER_FEATURE& feature) const;

		//!
		virtual void issueGPUTextureBarrier() {COpenGLExtensionHandler::extGlTextureBarrier();}

        //! needs to be "deleted" since its not refcounted
        virtual core::smart_refctd_ptr<IDriverFence> placeFence(const bool& implicitFlushWaitSameThread=false) override final
        {
            return core::make_smart_refctd_ptr<COpenGLDriverFence>(implicitFlushWaitSameThread);
        }

		//! \return Returns the name of the video driver. Example: In case of the Direct3D8
		//! driver, it would return "Direct3D8.1".
		virtual const wchar_t* getName() const;

		//! sets a viewport
		virtual void setViewPort(const core::rect<int32_t>& area);

		//! Returns type of video driver
		virtual E_DRIVER_TYPE getDriverType() const;

		//! get color format of the current color buffer
		virtual asset::E_FORMAT getColorFormat() const;

        /*
        virtual int32_t addHighLevelShaderMaterial(
            const char* vertexShaderProgram,
            const char* controlShaderProgram,
            const char* evaluationShaderProgram,
            const char* geometryShaderProgram,
            const char* pixelShaderProgram,
            uint32_t patchVertices=3,
            E_MATERIAL_TYPE baseMaterial=video::EMT_SOLID,
            IShaderConstantSetCallBack* callback=0,
            const char** xformFeedbackOutputs = NULL,
            const uint32_t& xformFeedbackOutputCount = 0,
            int32_t userData=0,
            const char* vertexShaderEntryPointName="main",
            const char* controlShaderEntryPointName="main",
            const char* evaluationShaderEntryPointName="main",
            const char* geometryShaderEntryPointName="main",
            const char* pixelShaderEntryPointName="main");
        */
		//! Returns a pointer to the IVideoDriver interface. (Implementation for
		//! IMaterialRendererServices)
		virtual IVideoDriver* getVideoDriver();

		//! Returns the maximum amount of primitives (mostly vertices) which
		//! the device is able to render with one drawIndexedTriangleList
		//! call.
		virtual uint32_t getMaximalIndicesCount() const;

        core::smart_refctd_ptr<ITexture> createGPUTexture(const ITexture::E_TEXTURE_TYPE& type, const uint32_t* size, uint32_t mipmapLevels, asset::E_FORMAT format = asset::EF_B8G8R8A8_UNORM) override;

        //!
        virtual IMultisampleTexture* addMultisampleTexture(const IMultisampleTexture::E_MULTISAMPLE_TEXTURE_TYPE& type, const uint32_t& samples, const uint32_t* size, asset::E_FORMAT format = asset::EF_B8G8R8A8_UNORM, const bool& fixedSampleLocations = false);

		//! A.
        virtual IGPUBufferView* addTextureBufferObject(IGPUBuffer* buf, asset::E_FORMAT format = asset::EF_R8G8B8A8_UNORM, const size_t& offset=0, const size_t& length=0);

        virtual IFrameBuffer* addFrameBuffer();

        //! Remove
        virtual void removeFrameBuffer(IFrameBuffer* framebuf);

        virtual void removeAllFrameBuffers();


		virtual bool setRenderTarget(IFrameBuffer* frameBuffer, bool setNewViewport=true);

		virtual void blitRenderTargets(IFrameBuffer* in, IFrameBuffer* out,
                                        bool copyDepth=true, bool copyStencil=true,
										core::recti srcRect=core::recti(0,0,0,0),
										core::recti dstRect=core::recti(0,0,0,0),
										bool bilinearFilter=false);


		//! Clears the ZBuffer.
		virtual void clearZBuffer(const float &depth=0.0);

		virtual void clearStencilBuffer(const int32_t &stencil);

		virtual void clearZStencilBuffers(const float &depth, const int32_t &stencil);

		virtual void clearColorBuffer(const E_FBO_ATTACHMENT_POINT &attachment, const int32_t* vals);
		virtual void clearColorBuffer(const E_FBO_ATTACHMENT_POINT &attachment, const uint32_t* vals);
		virtual void clearColorBuffer(const E_FBO_ATTACHMENT_POINT &attachment, const float* vals);

		virtual void clearScreen(const E_SCREEN_BUFFERS &buffer, const float* vals) override;
		virtual void clearScreen(const E_SCREEN_BUFFERS &buffer, const uint32_t* vals) override;

        const CDerivativeMapCreator* getDerivativeMapCreator() const override { return DerivativeMapCreator; };

		//! Enable/disable a clipping plane.
		//! There are at least 6 clipping planes available for the user to set at will.
		//! \param index: The plane index. Must be between 0 and MaxUserClipPlanes.
		//! \param enable: If true, enable the clipping plane else disable it.
		virtual void enableClipPlane(uint32_t index, bool enable);

		//! Returns the graphics card vendor name.
		virtual std::string getVendorInfo() {return VendorName;}

		//!
		const size_t& getMaxConcurrentShaderInvocations() const {return maxConcurrentShaderInvocations;}

		//!
		const uint32_t& getMaxShaderComputeUnits() const {return maxShaderComputeUnits;}

		//!
		const size_t& getMaxShaderInvocationsPerALU() const {return maxALUShaderInvocations;}

#ifdef _IRR_COMPILE_WITH_OPENCL_
        const cl_device_id& getOpenCLAssociatedDevice() const {return clDevice;}

        const size_t& getOpenCLAssociatedDeviceID() const {return clDeviceIx;}
        const size_t& getOpenCLAssociatedPlatformID() const {return clPlatformIx;}
#endif // _IRR_COMPILE_WITH_OPENCL_

        struct SAuxContext
        {
        //public:
            _IRR_STATIC_INLINE_CONSTEXPR size_t maxVAOCacheSize = 0x1u<<14; //make this cache configurable

            SAuxContext() : threadId(std::thread::id()), ctx(NULL),
                            CurrentFBO(0), CurrentRendertargetSize(0,0)
            {
                VAOMap.reserve(maxVAOCacheSize);
            }

            void flushState(GL_STATE_BITS stateBits);

            SOpenGLState currentState;
            SOpenGLState nextState;
        //private:
            std::thread::id threadId;
            uint8_t ID; //index in array of contexts, just to be easier in use
            #ifdef _IRR_WINDOWS_API_
                HGLRC ctx;
            #endif
            #ifdef _IRR_COMPILE_WITH_X11_DEVICE_
                GLXContext ctx;
                GLXPbuffer pbuff;
            #endif
            #ifdef _IRR_COMPILE_WITH_OSX_DEVICE_
                AppleMakesAUselessOSWhichHoldsBackTheGamingIndustryAndSabotagesOpenStandards ctx;
            #endif

            //! FBOs
            core::vector<IFrameBuffer*>  FrameBuffers;
            COpenGLFrameBuffer*         CurrentFBO;
            core::dimension2d<uint32_t> CurrentRendertargetSize;

            //!
            core::vector<SOpenGLState::HashVAOPair> VAOMap;

            void updateNextState_pipelineAndRaster(const IGPURenderpassIndependentPipeline* _pipeline);

            inline size_t getVAOCacheSize() const
            {
                return VAOMap.size();
            }

            inline void freeUpVAOCache(bool exitOnFirstDelete)
            {
                for(auto it = VAOMap.begin(); VAOMap.size()>maxVAOCacheSize&&it!=VAOMap.end();)
                {
                    if (it->first==currentState.vertexInputParams.vao.first)
                        continue;

                    if (CNullDriver::ReallocationCounter-it->second.lastValidated>1000) //maybe make this configurable
                    {
                        COpenGLExtensionHandler::extGlDeleteVertexArrays(1, &it->second.GLname);
                        it = VAOMap.erase(it);
                        if (exitOnFirstDelete)
                            return;
                    }
                    else
                        it++;
                }
            }
        };


		//!
		virtual uint32_t getRequiredUBOAlignment() const {return COpenGLExtensionHandler::reqUBOAlignment;}

		//!
		virtual uint32_t getRequiredSSBOAlignment() const {return COpenGLExtensionHandler::reqSSBOAlignment;}

		//!
		virtual uint32_t getRequiredTBOAlignment() const {return COpenGLExtensionHandler::reqTBOAlignment;}

		//!
		virtual uint32_t getMinimumMemoryMapAlignment() const {return COpenGLExtensionHandler::minMemoryMapAlignment;}

        //!
        virtual uint32_t getMaxComputeWorkGroupSize(uint32_t _dimension) const { return COpenGLExtensionHandler::MaxComputeWGSize[_dimension]; }

        //!
        virtual uint64_t getMaxUBOSize() const override { return COpenGLExtensionHandler::maxUBOSize; }

        //!
        virtual uint64_t getMaxSSBOSize() const override { return COpenGLExtensionHandler::maxSSBOSize; }

        //!
        virtual uint64_t getMaxTBOSize() const override { return COpenGLExtensionHandler::maxTBOSize; }

        //!
        virtual uint64_t getMaxBufferSize() const override { return COpenGLExtensionHandler::maxBufferSize; }

    private:
        SAuxContext* getThreadContext_helper(const bool& alreadyLockedMutex, const std::thread::id& tid = std::this_thread::get_id());

        void cleanUpContextBeforeDelete();


        //COpenGLDriver::CGPUObjectFromAssetConverter
        class CGPUObjectFromAssetConverter;
        friend class CGPUObjectFromAssetConverter;

        using PipelineMapKeyT = std::pair<std::array<core::smart_refctd_ptr<IGPUSpecializedShader>, 5u>, std::thread::id>;
        core::map<PipelineMapKeyT, GLuint> Pipelines;

        bool runningInRenderDoc;

		//! inits the parts of the open gl driver used on all platforms
		bool genericDriverInit();

		//! returns a device dependent texture from a software surface (IImage)
		virtual core::smart_refctd_ptr<video::ITexture> createDeviceDependentTexture(const ITexture::E_TEXTURE_TYPE& type, const uint32_t* size, uint32_t mipmapLevels, const io::path& name, asset::E_FORMAT format = asset::EF_B8G8R8A8_UNORM);

		// returns the current size of the screen or rendertarget
		virtual const core::dimension2d<uint32_t>& getCurrentRenderTargetSize() const;

		void createMaterialRenderers();

		core::stringw Name;

		std::string VendorName;

		//! Color buffer format
		asset::E_FORMAT ColorFormat; //FIXME

		SIrrlichtCreationParameters Params;

		#ifdef _IRR_WINDOWS_API_
			HDC HDc; // Private GDI Device Context
			HWND Window;
		#ifdef _IRR_COMPILE_WITH_WINDOWS_DEVICE_
			CIrrDeviceWin32 *Win32Device;
		#endif
		#endif
		#ifdef _IRR_COMPILE_WITH_X11_DEVICE_
			GLXDrawable Drawable;
			Display* X11Display;
			CIrrDeviceLinux *X11Device;
		#endif
		#ifdef _IRR_COMPILE_WITH_OSX_DEVICE_
			CIrrDeviceMacOSX *OSXDevice;
		#endif
		#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
			CIrrDeviceSDL *SDLDevice;
		#endif

        size_t maxALUShaderInvocations;
        size_t maxConcurrentShaderInvocations;
        uint32_t maxShaderComputeUnits;
#ifdef _IRR_COMPILE_WITH_OPENCL_
        cl_device_id clDevice;
        size_t clPlatformIx, clDeviceIx;
#endif // _IRR_COMPILE_WITH_OPENCL_

        FW_Mutex* glContextMutex;
		SAuxContext* AuxContexts;
        CDerivativeMapCreator* DerivativeMapCreator;
        core::smart_refctd_ptr<const asset::IGLSLCompiler> GLSLCompiler;

		E_DEVICE_TYPE DeviceType;
	};

} // end namespace video
} // end namespace irr


#endif // _IRR_COMPILE_WITH_OPENGL_
#endif

