/* -LICENSE-START-
** Copyright (c) 2018 Blackmagic Design
**
** Permission is hereby granted, free of charge, to any person or organization
** obtaining a copy of the software and accompanying documentation covered by
** this license (the "Software") to use, reproduce, display, distribute,
** execute, and transmit the Software, and to prepare derivative works of the
** Software, and to permit third-parties to whom the Software is furnished to
** do so, all subject to the following:
** 
** The copyright notices in the Software and this entire statement, including
** the above license grant, this restriction and the following disclaimer,
** must be included in all copies of the Software, in whole or in part, and
** all derivative works of the Software, unless such copies or derivative
** works are solely in the form of machine-executable object code generated by
** a source language processor.
** 
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
** -LICENSE-END-
*/
//
// HDRVideoFrame.cpp
//

#include <cstring>
#include "HDRVideoFrame.h"


#define CompareREFIID(iid1, iid2)	(memcmp(&iid1, &iid2, sizeof(REFIID)) == 0)

HDRVideoFrame::HDRVideoFrame(com_ptr<IDeckLinkMutableVideoFrame> frame, HDRMetadata& metadata)
	: m_videoFrame(frame), m_metadata(metadata), m_refCount(1)
{
}

/// IUnknown methods

HRESULT HDRVideoFrame::QueryInterface(REFIID iid, LPVOID *ppv)
{
	CFUUIDBytes		iunknown;
	HRESULT			result		= S_OK;

	if (ppv == nullptr)
		return E_INVALIDARG;

	// Initialise the return result
	*ppv = nullptr;

	iunknown = CFUUIDGetUUIDBytes(IUnknownUUID);
	if (std::memcmp(&iid, &iunknown, sizeof(REFIID)) == 0)
	{
		*ppv = this;
		AddRef();
	}
	else if (std::memcmp(&iid, &IID_IDeckLinkVideoFrame, sizeof(REFIID)) == 0)
	{
		*ppv = static_cast<IDeckLinkVideoFrame*>(this);
		AddRef();
	}
	else if (std::memcmp(&iid, &IID_IDeckLinkVideoFrameMetadataExtensions, sizeof(REFIID)) == 0)
	{
		*ppv = static_cast<IDeckLinkVideoFrameMetadataExtensions*>(this);
		AddRef();
	}
	else
	{
		result = E_NOINTERFACE;
	}

	return result;
}

ULONG HDRVideoFrame::AddRef(void)
{
	return ++m_refCount;
}

ULONG HDRVideoFrame::Release(void)
{
	ULONG newRefValue = --m_refCount;

	if (newRefValue == 0)
		delete this;

	return newRefValue;
}

/// IDeckLinkVideoFrameMetadataExtensions methods

HRESULT HDRVideoFrame::GetInt(BMDDeckLinkFrameMetadataID metadataID, int64_t* value)
{
	HRESULT result = S_OK;

	switch (metadataID)
	{
		case bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc:
			*value = m_metadata.EOTF;
			break;

		case bmdDeckLinkFrameMetadataColorspace:
			// Colorspace is fixed for this sample
			*value = bmdColorspaceRec2020;
			break;

		default:
			value = nullptr;
			result = E_INVALIDARG;
	}

	return result;
}

HRESULT HDRVideoFrame::GetFloat(BMDDeckLinkFrameMetadataID metadataID, double* value)
{
	HRESULT result = S_OK;

	switch (metadataID)
	{
		case bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedX: 
			*value = m_metadata.referencePrimaries.RedX;
			break;

		case bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedY:
			*value = m_metadata.referencePrimaries.RedY;
			break;

		case bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenX:
			*value = m_metadata.referencePrimaries.GreenX;
			break;

		case bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenY:
			*value = m_metadata.referencePrimaries.GreenY;
			break;

		case bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueX:
			*value = m_metadata.referencePrimaries.BlueX;
			break;

		case bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueY:
			*value = m_metadata.referencePrimaries.BlueY;
			break;

		case bmdDeckLinkFrameMetadataHDRWhitePointX:
			*value = m_metadata.referencePrimaries.WhiteX;
			break;

		case bmdDeckLinkFrameMetadataHDRWhitePointY:
			*value = m_metadata.referencePrimaries.WhiteY;
			break;

		case bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance:
			*value = m_metadata.maxDisplayMasteringLuminance;
			break;

		case bmdDeckLinkFrameMetadataHDRMinDisplayMasteringLuminance:
			*value = m_metadata.minDisplayMasteringLuminance;
			break;

		case bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel:
			*value = m_metadata.maxCLL;
			break;

		case bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel:
			*value = m_metadata.maxFALL;
			break;

		default:
			value = nullptr;
			result = E_INVALIDARG;
	}

	return result;
}

HRESULT HDRVideoFrame::GetFlag(BMDDeckLinkFrameMetadataID, bool* value)
{
	// Not expecting GetFlag
	value = nullptr;
	return E_INVALIDARG;
}

HRESULT HDRVideoFrame::GetString(BMDDeckLinkFrameMetadataID, const char** value)
{
	// Not expecting GetString
	value = nullptr;
	return E_INVALIDARG;
}
