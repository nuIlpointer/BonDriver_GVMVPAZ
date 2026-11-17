#include <initguid.h>
#include "tswriter.h"
#include "common.h"
#include <vector>

// Define KSDATAFORMAT_SUBTYPE_BDA_MPEG2_TRANSPORT: {F4AEB342-0329-4FDD-A8FD-4AFF4926C978}
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_BDA_MPEG2_TRANSPORT_LOCAL, 
    0xf4aeb342, 0x0329, 0x4fdd, 0xa8, 0xfd, 0x4a, 0xff, 0x49, 0x26, 0xc9, 0x78);

CUnknown * WINAPI CTsWriter::CreateInstance(LPUNKNOWN pUnk, HRESULT * phr)
{
	CTsWriter *pFilter = new CTsWriter(NAME("TS Writer Filter"), pUnk, phr);
	if (pFilter == NULL)
	{
		*phr = E_OUTOFMEMORY;
	}
	return pFilter;
}

CTsWriter::CTsWriter(LPCTSTR pName, LPUNKNOWN pUnk, HRESULT * phr)
	: CTransInPlaceFilter(pName, pUnk, CLSID_TsWriter, phr),
	  m_pRecv(NULL),
	  m_pParam(NULL)
{
}

HRESULT CTsWriter::Transform(IMediaSample * pSample)
{
	PBYTE pbData;
	HRESULT hr = pSample->GetPointer(&pbData);
	if (SUCCEEDED(hr)) {
		Write(pbData, pSample->GetActualDataLength());
	}

	return S_OK;
}

HRESULT CTsWriter::CheckInputType(const CMediaType * mtIn)
{
	if (!mtIn) return E_POINTER;
	if (mtIn->subtype == MEDIASUBTYPE_MPEG2_TRANSPORT ||
	    mtIn->subtype == KSDATAFORMAT_SUBTYPE_BDA_MPEG2_TRANSPORT_LOCAL) {
		return S_OK;
	}
	return S_OK;
}

STDMETHODIMP CTsWriter::NonDelegatingQueryInterface(REFIID riid, void ** ppv)
{
	if (riid == IID_ITsWriter) {
		return GetInterface((ITsWriter *)this, ppv);
	}
	return CTransformFilter::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP CTsWriter::SetCallBackRecv(RECV_PROC pRecv, void * pParam)
{
	CAutoLock lock_it(&m_Lock);
	m_pRecv = pRecv;
	m_pParam = pParam;
	OutputDebug(L"  [CTsWriter::SetCallBackRecv] this=%p pRecv=%p pParam=%p\n", this, pRecv, pParam);
	return S_OK;
}

HRESULT CTsWriter::Write(PBYTE pbData, LONG lDataLength)
{
	static LONG s_writeLog = 0;
	if (s_writeLog < 5) {
		LONG idx = InterlockedIncrement(&s_writeLog);
		if (idx <= 5) {
			CAutoLock lock_it(&m_Lock);
			OutputDebug(L"  [CTsWriter::Write] #%ld this=%p len=%ld m_pRecv=%p bytes=%02X %02X %02X %02X %02X %02X %02X %02X\n",
				idx, this, lDataLength, m_pRecv,
				(lDataLength > 0 && pbData) ? pbData[0] : 0, (lDataLength > 1 && pbData) ? pbData[1] : 0,
				(lDataLength > 2 && pbData) ? pbData[2] : 0, (lDataLength > 3 && pbData) ? pbData[3] : 0,
				(lDataLength > 4 && pbData) ? pbData[4] : 0, (lDataLength > 5 && pbData) ? pbData[5] : 0,
				(lDataLength > 6 && pbData) ? pbData[6] : 0, (lDataLength > 7 && pbData) ? pbData[7] : 0);
		}
	}

	if (lDataLength < 188 || !pbData) {
		return S_OK;
	}

	// GV-MVP/AZ Stream Out delivers 192-byte TTS packets ([4B 27MHz big-endian timestamp] + [188B TS]).
	// Do NOT check pbData[0] != 0x47: at ~44.1s (0x47000000 / 27MHz) timestamp byte 0 itself equals 0x47!
	// Strip the 4-byte prefix cleanly whenever every checked 192-byte packet aligns on sync byte 0x47 at +4.
	std::vector<BYTE> stripped;
	PBYTE outPtr = pbData;
	size_t outSize = (size_t)lDataLength;
	if (lDataLength >= 192 && (lDataLength % 192) == 0) {
		LONG count = lDataLength / 192;
		LONG checkCount = (count < 4) ? count : 4;
		bool isTts = true;
		for (LONG i = 0; i < checkCount; i++) {
			if (pbData[(size_t)i * 192 + 4] != 0x47) {
				isTts = false;
				break;
			}
		}
		if (isTts) {
			stripped.resize((size_t)count * 188);
			for (LONG i = 0; i < count; i++) {
				memcpy(&stripped[(size_t)i * 188], pbData + (size_t)i * 192 + 4, 188);
			}
			outPtr = stripped.data();
			outSize = stripped.size();
		}
	}

	{
		CAutoLock lock_it(&m_Lock);
		if (m_pRecv != NULL) {
			m_pRecv(m_pParam, outPtr, outSize);
		}
	}
    return S_OK;
}
