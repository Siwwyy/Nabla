// Copyright (C) 2017- Mateusz 'DevSH' Kielan
// This file is part of the "IrrlichtBAW" engine.
// For conditions of distribution and use, see copyright notice in irrlicht.h

#ifndef __I_CPU_IMAGE_H_INCLUDED__
#define __I_CPU_IMAGE_H_INCLUDED__

#include "irr/core/core.h"

#include "irr/asset/IAsset.h"
#include "irr/asset/ICPUBuffer.h"
#include "irr/asset/IImage.h"

namespace irr
{
namespace asset
{

class ICPUImage final : public IImage, public IAsset
{
	public:
		inline static core::smart_refctd_ptr<ICPUImage> create(
							E_IMAGE_CREATE_FLAGS _flags,
							E_IMAGE_TYPE _type,
							E_FORMAT _format,
							const VkExtent3D& _extent,
							uint32_t _mipLevels = 1u,
							uint32_t _arrayLayers = 1u,
							E_SAMPLE_COUNT_FLAGS _samples = ESCF_1_BIT)
		{
			if (validateCreationParameters(_flags,_type,_format,_extent,_mipLevels,_arrayLayers,_samples))
				return nullptr;

			// initialLayout must be VK_IMAGE_LAYOUT_UNDEFINED or VK_IMAGE_LAYOUT_PREINITIALIZED.

			return core::make_smart_refctd_ptr<ICPUImage>(_flags,_type,_format,_extent,_mipLevels,_arrayLayers,_samples);
		}

        inline void convertToDummyObject() override
        {
            regions = nullptr;
        }

        inline E_TYPE getAssetType() const override { return IAsset::ET_IMAGE; }

        virtual size_t conservativeSizeEstimate() const override
		{
			return sizeof(IImage)-sizeof(IDescriptor)+sizeof(void*)*2u;
		}

		virtual bool validateCopies(const SImageCopy* pRegionsBegin, const SImageCopy* pRegionsEnd, const ICPUImage* src)
		{
			// GPU command
#ifdef _IRR_DEBUG // TODO: When Vulkan comes
	// image offset and extent must respect granularity requirements
	// buffer has memory bound (with sparse exceptions)
	// check buffer has transfer usage flag
	// format features of dstImage contain transfer dst bit
	// dst image not created subsampled
	// etc.
#endif
			return validateCopies_template(pRegionsBegin, pRegionsEnd, src);
		}

		inline auto* getBuffer() { return buffer.get(); }
		inline const auto* getBuffer() const { return buffer.get(); }

		inline auto* getRegions() { return regions->data(); }
		inline const auto* getRegions() const { return regions->data(); }

		//! regions will be copied and sorted
		inline bool setBufferAndRegions(core::smart_refctd_ptr<ICPUBuffer>&& _buffer, const core::smart_refctd_dynamic_array<const IImage::SBufferCopy>& _regions)
		{
			if (!IImage::validateCopies(_regions->begin(),_regions->end(),_buffer.get()))
				return false;
		
			buffer = _buffer;
			regions = core::make_refctd_dynamic_array<core::smart_refctd_dynamic_array<const IImage::SBufferCopy> >(_regions);
			sortRegionsByMipMapLevel();
			return true;
		}

		/*
        inline void* getRowPointer(const SBufferCopy& region, uint32_t relMip, uint32_t relZ, uint32_t relY)
		{
            return const_cast<void*>(getRowPointer(region,relMip,relZ,relY));
		}
        inline const void* getRowPointer(const SBufferCopy& region, uint32_t relMip, uint32_t relZ, uint32_t relY) const
        {
            if (asset::isBlockCompressionFormat(getColorFormat()))
                return nullptr;

            if (row<minCoord[0]||row>=maxCoord[0])
                return nullptr;
            if (slice<minCoord[1]||slice>=maxCoord[1])
                return nullptr;

            size_t size[3] = {maxCoord[0]-minCoord[0],maxCoord[1]-minCoord[1],maxCoord[2]-minCoord[2]};
            row     -= minCoord[0];
            slice   -= minCoord[1];
            return reinterpret_cast<uint8_t*>(data)+(slice*size[1]+row)*getPitchIncludingAlignment();
        }
		

        //!
        inline uint32_t getPitchIncludingAlignment() const
        {
            if (isBlockCompressionFormat(getColorFormat()))
                return 0; //special error val

			auto lineBytes = getBytesPerPixel() * (maxCoord[0]-minCoord[0]);
			assert(lineBytes.getNumerator()%lineBytes.getDenominator() == 0u);
            return (lineBytes.getNumerator()/lineBytes.getDenominator()+unpackAlignment-1)&(~(unpackAlignment-1u));
        }
*/

    protected:
		ICPUImage(	E_IMAGE_CREATE_FLAGS _flags,
					E_IMAGE_TYPE _type,
					E_FORMAT _format,
					const VkExtent3D& _extent,
					uint32_t _mipLevels,
					uint32_t _arrayLayers,
					E_SAMPLE_COUNT_FLAGS _samples) :
						IImage(_flags,_type,_format,_extent,_mipLevels,_arrayLayers,_samples)
		{
		}

		virtual ~ICPUImage() = default;
		
		
		core::smart_refctd_ptr<asset::ICPUBuffer>				buffer;
		core::smart_refctd_dynamic_array<IImage::SBufferCopy>	regions;

	private:
		inline void sortRegionsByMipMapLevel()
		{
			std::sort(regions->begin(), regions->end(), [](const IImage::SBufferCopy& _a, const IImage::SBufferCopy& _b)
				{
					if (_a.imageSubresource.mipLevel==_b.imageSubresource.mipLevel)
					{
						if (_a.imageSubresource.baseArrayLayer==_b.imageSubresource.baseArrayLayer)
						{
							if (_a.imageOffset.x==_b.imageOffset.x)
							{
								if (_a.imageOffset.y==_b.imageOffset.y)
									return _a.imageOffset.z<_b.imageOffset.z;
								else
									return _a.imageOffset.y<_b.imageOffset.y;
							}
							else
								return _a.imageOffset.x<_b.imageOffset.x;
						}
						else
							return _a.imageSubresource.baseArrayLayer<_b.imageSubresource.baseArrayLayer;
					}
					else
						return _a.imageSubresource.mipLevel<_b.imageSubresource.mipLevel;
				}
			);
		}
};

} // end namespace video
} // end namespace irr

#endif

