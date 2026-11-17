// BonTuner.cpp: CBonTuner NXﾌイve[V
//
//////////////////////////////////////////////////////////////////////

#include "common.h"

#include "BonTuner.h"

#include <Windows.h>
#include <string>
#include <regex>

#include <rpc.h>
#include <bcrypt.h>

#include <DShow.h>

#include "tswriter.h"

// KSCATEGORY_...
#include <ks.h>
#pragma warning (push)
#pragma warning (disable: 4091)
#include <ksmedia.h>
#pragma warning (pop)
#include <bdatypes.h>
#include <bdamedia.h>
#include <bdaiface.h>

// bstr_t
#include <comdef.h>

#include "CIniFileAccess.h"
#include "WaitWithMsg.h"

#pragma comment(lib, "Rpcrt4.lib")
#pragma comment(lib, "Bcrypt.lib")

#ifdef _DEBUG
#pragma comment(lib, "strmbasd.lib")
#else
#pragma comment(lib, "strmbase.lib")
#endif

#pragma comment(lib, "winmm.lib")

FILE *g_fpLog = NULL;


//////////////////////////////////////////////////////////////////////
// ﾃ的o
//////////////////////////////////////////////////////////////////////

// Dllﾌジ[nh
HMODULE CBonTuner::st_hModule = NULL;

// 黷ｽCBontunerCX^Xﾌ一覧
std::list<CBonTuner*> CBonTuner::st_InstanceList;

// st_InstanceListp
CRITICAL_SECTION CBonTuner::st_LockInstanceList;

// Kvﾈ静的
void CBonTuner::Init(HMODULE hModule)
{
	st_hModule = hModule;
	::InitializeCriticalSection(&st_LockInstanceList);
	return;
}

// ﾃ的ﾏ?
void CBonTuner::Finalize(void)
{
	// ﾌイX^Xcﾄ?
	for (auto it = st_InstanceList.begin(); it != st_InstanceList.end();) {
		SAFE_RELEASE(*it);
		it = st_InstanceList.erase(it);
	}

	::DeleteCriticalSection(&CBonTuner::st_LockInstanceList);

	// fobOOt@Cﾌク[Y
	CloseDebugLog();
	return;
}

//////////////////////////////////////////////////////////////////////
// CX^X\bh
//////////////////////////////////////////////////////////////////////
#pragma warning(disable : 4273)
extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver()
{
	return (IBonDriver *) new CBonTuner;
}
#pragma warning(default : 4273)

//////////////////////////////////////////////////////////////////////
// \z/
//////////////////////////////////////////////////////////////////////
CBonTuner::CBonTuner()
	: m_nToneWait(100),
	m_nLockWait(2000),
	m_nLockWaitDelay(0),
	m_nLockWaitRetry(0),
	m_bLockTwice(FALSE),
	m_nLockTwiceDelay(100),
	m_nWatchDogSignalLocked(0),
	m_nWatchDogBitRate(0),
	m_nReOpenWhenGiveUpReLock(0),
	m_bTryAnotherTuner(FALSE),
	m_bBackgroundChannelLock(FALSE),
	m_nSignalLevelCalcType(eSignalLevelCalcTypeSSStrength),
	m_bSignalLevelGetTypeSS(FALSE),
	m_bSignalLevelGetTypeTuner(FALSE),
	m_bSignalLevelGetTypeDemodSS(FALSE),
	m_bSignalLevelGetTypeBR(FALSE),
	m_bSignalLevelNeedStrength(FALSE),
	m_bSignalLevelNeedQuality(FALSE),
	m_fStrengthCoefficient(1),
	m_fQualityCoefficient(1),
	m_fStrengthBias(0),
	m_fQualityBias(0),
	m_fStrength(0),
	m_fQuality(0),
	m_nSignalLockedJudgeType(eSignalLockedJudgeTypeSS),
	m_bSignalLockedJudgeTypeSS(FALSE),
	m_bSignalLockedJudgeTypeTuner(FALSE),
	m_bSignalLockedJudgeTypeDemodSS(FALSE),
	m_nBuffSize(188 * 1024),
	m_nMaxBuffCount(512),
	m_nWaitTsCount(1),
	m_nWaitTsSleep(100),
	m_bDeleteNullPackets(FALSE),
	m_bAlwaysAnswerLocked(FALSE),
	m_nThreadPriorityCOM(THREAD_PRIORITY_ERROR_RETURN),
	m_nThreadPriorityDecode(THREAD_PRIORITY_ERROR_RETURN),
	m_nThreadPriorityStream(THREAD_PRIORITY_ERROR_RETURN),
	m_nPeriodicTimer(0),
	m_sIniFilePath(L""),
	m_hOnStreamEvent(NULL),
	m_hOnDecodeEvent(NULL),
	m_LastBuff(NULL),
	m_bRecvStarted(FALSE),
	m_hStreamThread(NULL),
	m_bIsSetStreamThread(FALSE),
	m_hSemaphore(NULL),
	m_hPowerSetFlag(NULL),
	m_hPowerSetLock(NULL),
	m_dwROTRegister(0),
	m_pDSFilterEnumTuner(NULL),
	m_pDSFilterEnumCapture(NULL),
	m_nNetworkProvider(eNetworkProviderAuto),
	m_nDefaultNetwork(eDefaultNetworkSPHD),
	m_bRegisterGraphInROT(FALSE),
	m_bOpened(FALSE),
	m_dwTargetSpace(CBonTuner::SPACE_INVALID),
	m_dwCurSpace(CBonTuner::SPACE_INVALID),
	m_dwTargetChannel(CBonTuner::CHANNEL_INVALID),
	m_dwCurChannel(CBonTuner::CHANNEL_INVALID),
	m_nCurTone(CBonTuner::TONE_UNKNOWN),
	m_bIsEnabledTSMF(FALSE),
	m_lResetFilter(0),
	m_PacketSize(0),
	m_hModuleTunerSpecials(NULL),
	m_pIBdaSpecials(NULL),
	m_pIBdaSpecials2(NULL)
{
	// CX^XXgﾉ使o^
	::EnterCriticalSection(&st_LockInstanceList);
	st_InstanceList.push_back(this);
	::LeaveCriticalSection(&st_LockInstanceList);

	setlocale(LC_CTYPE, "ja_JP.SJIS");

	HANDLE h = ::GetCurrentProcess();
	::DuplicateHandle(h, h, h, &m_hProcess, 0, FALSE, DUPLICATE_SAME_ACCESS);

	ReadIniFile();

	m_TsBuff.SetSize(m_nBuffSize, m_nMaxBuffCount);
	m_DecodedTsBuff.SetSize(0, m_nMaxBuffCount);

	// COMpXbhN
	m_aCOMProc.hThread = ::CreateThread(NULL, 0, CBonTuner::COMProcThread, this, 0, NULL);

	// XbhvCIeB
	if (m_aCOMProc.hThread != NULL && m_nThreadPriorityCOM != THREAD_PRIORITY_ERROR_RETURN) {
		::SetThreadPriority(m_aCOMProc.hThread, m_nThreadPriorityCOM);
	}

	// timeBeginPeriod()ﾅ設定すWindowsﾌ最戎C}\(msec)
	if (m_nPeriodicTimer != 0) {
		if (timeBeginPeriod(m_nPeriodicTimer) == TIMERR_NOCANDO) {
			m_nPeriodicTimer = 0;
		}
	}
}

CBonTuner::~CBonTuner()
{
	OutputDebug(L"~CBonTuner called.\n");
	CloseTuner();

	// timeBeginPeriod()ﾌ系
	if (m_nPeriodicTimer != 0) {
		timeEndPeriod(m_nPeriodicTimer);
	}

	// COMpXbhI
	if (m_aCOMProc.hThread) {
		::SetEvent(m_aCOMProc.hTerminateRequest);
		::WaitForSingleObject(m_aCOMProc.hThread, INFINITE);
		SAFE_CLOSE_HANDLE(m_aCOMProc.hThread);
	}

	SAFE_CLOSE_HANDLE(m_hStreamThread);
	SAFE_CLOSE_HANDLE(m_hProcess);

	// CX^XXg邇ｩg
	::EnterCriticalSection(&st_LockInstanceList);
	st_InstanceList.remove(this);
	::LeaveCriticalSection(&st_LockInstanceList);
}

/////////////////////////////////////
//
// IBonDriver2 APIs
//
/////////////////////////////////////

const BOOL CBonTuner::OpenTuner(void)
{
	if (m_aCOMProc.hThread == NULL)
		return FALSE;

	DWORD dw;
	BOOL ret = FALSE;

	::EnterCriticalSection(&m_aCOMProc.csLock);

	m_aCOMProc.nRequest = enumCOMRequest::eCOMReqOpenTuner;
	::SetEvent(m_aCOMProc.hReqEvent);
	HANDLE h[2] = {
		m_aCOMProc.hEndEvent,
		m_aCOMProc.hThread
	};
	dw = ::WaitForMultipleObjects(2, h, FALSE, INFINITE);
	if (dw == WAIT_OBJECT_0) {
		ret = m_aCOMProc.uRetVal.OpenTuner;
	}

	::LeaveCriticalSection(&m_aCOMProc.csLock);
	return ret;
}

const BOOL CBonTuner::_OpenTuner(void)
{
	if (m_bOpened)
		return TRUE;

	HRESULT hr;

	do {
		// tB^Otﾌ作成
		if (FAILED(hr = InitializeGraphBuilder()))
			break;

		// `[jOXy[Xﾌ?
		if (FAILED(hr = CreateTuningSpace()))
			break;

		// lbg[NvoC_
		if (FAILED(hr = LoadNetworkProvider()))
			break;

		// `[jOXy[X
		if (FAILED(hr = InitTuningSpace()))
			break;

		// [hﾗＡ[iELv`ﾌスg
		if (m_UsableTunerCaptureList.empty() && FAILED(hr = InitDSFilterEnum()))
			break;

		// `[iELv`ﾈ故構zﾆ市
		if (FAILED(hr = LoadAndConnectDevice()))
			break;

		OutputDebug(L"Build graph Successfully.\n");

		// `[iﾌ信ﾔ取得pC^[tF[Xﾌ取得isﾄＴj
		hr = LoadTunerSignalStatisticsTunerNode();
	hr = LoadTunerSignalStatisticsDemodNode();

		// TSMCxg
		m_hOnStreamEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);

		// DecodeCxg
		m_hOnDecodeEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);

		// DecodepXbhN
		m_aDecodeProc.hThread = ::CreateThread(NULL, 0, CBonTuner::DecodeProcThread, this, 0, NULL);

		// XbhvCIeB
		if (m_aDecodeProc.hThread != NULL && m_nThreadPriorityDecode != THREAD_PRIORITY_ERROR_RETURN) {
			::SetThreadPriority(m_aDecodeProc.hThread, m_nThreadPriorityDecode);
		}

		// R[obNﾖ浸bg
		StartRecv();

		PowerSetOnOpened();

		m_bOpened = TRUE;

		return TRUE;

	} while(0);

	// ﾉ釘ﾆとは臥かﾌエ[ﾅ市
	_CloseTuner();

	return FALSE;
}

void CBonTuner::CloseTuner(void)
{
	if (m_aCOMProc.hThread == NULL)
		return;

	DWORD dw;

	::EnterCriticalSection(&m_aCOMProc.csLock);

	m_aCOMProc.nRequest = enumCOMRequest::eCOMReqCloseTuner;
	::SetEvent(m_aCOMProc.hReqEvent);
	HANDLE h[2] = {
		m_aCOMProc.hEndEvent,
		m_aCOMProc.hThread
	};
	dw = ::WaitForMultipleObjects(2, h, FALSE, INFINITE);

	::LeaveCriticalSection(&m_aCOMProc.csLock);
	return;
}

void CBonTuner::_CloseTuner(void)
{
	m_bOpened = FALSE;

	PowerSetOnClosing();

	// Ot~
	StopGraph();

	// R[obNﾖ厨
	StopRecv();

	// DecodepXbhI
	if (m_aDecodeProc.hThread) {
		::SetEvent(m_aDecodeProc.hTerminateRequest);
		WaitForSingleObjectWithMessageLoop(m_aDecodeProc.hThread, INFINITE);
		SAFE_CLOSE_HANDLE(m_aDecodeProc.hThread);
	}

	// DecodeCxgJ
	SAFE_CLOSE_HANDLE(m_hOnDecodeEvent);

	// TSMCxg
	SAFE_CLOSE_HANDLE(m_hOnStreamEvent);

	// obt@
	PurgeTsStream();
	SAFE_DELETE(m_LastBuff);

	// `[iﾌ信ﾔ取得pC^[tF[X
	UnloadTunerSignalStatistics();

	// Ot
	CleanupGraph();

	m_dwTargetSpace = m_dwCurSpace = CBonTuner::SPACE_INVALID;
	m_dwTargetChannel = m_dwCurChannel = CBonTuner::CHANNEL_INVALID;
	m_nCurTone = CBonTuner::TONE_UNKNOWN;

	if (m_hSemaphore) {
		try {
			::ReleaseSemaphore(m_hSemaphore, 1, NULL);
			SAFE_CLOSE_HANDLE(m_hSemaphore);
		} catch (...) {
			OutputDebug(L"Exception in ReleaseSemaphore.\n");
		}
	}

	return;
}

const BOOL CBonTuner::SetChannel(const BYTE byCh)
{
	// IBonDriver (not IBonDriver2) pC^[tF[X; obsolete?
	return SetChannel(0UL, DWORD(byCh));
}

const float CBonTuner::GetSignalLevel(void)
{
	if (m_aCOMProc.hThread == NULL)
		return FALSE;

	DWORD dw;
	float ret = 0.0F;

	::EnterCriticalSection(&m_aCOMProc.csLock);

	m_aCOMProc.nRequest = enumCOMRequest::eCOMReqGetSignalLevel;
	::SetEvent(m_aCOMProc.hReqEvent);
	HANDLE h[2] = {
		m_aCOMProc.hEndEvent,
		m_aCOMProc.hThread
	};
	dw = ::WaitForMultipleObjects(2, h, FALSE, INFINITE);
	if (dw == WAIT_OBJECT_0) {
		ret = m_aCOMProc.uRetVal.GetSignalLevel;
	}

	::LeaveCriticalSection(&m_aCOMProc.csLock);
	return ret;
}

const float CBonTuner::_GetSignalLevel(void)
{
	if (!m_bOpened)
		return -1.0F;

	HRESULT hr;
	float f = 0.0F;

	// rbg[gﾔれ合
	if (m_bSignalLevelGetTypeBR) {
		return (float)m_BitRate.GetRate();
	}

	// IBdaSpecials2ﾅ有ﾖ石?
	if (m_pIBdaSpecials2 && (hr = m_pIBdaSpecials2->GetSignalStrength(&f)) != E_NOINTERFACE) {
		return f;
	}

	//   get_SignalQuality Mﾌ品 1 ` 100 ﾌ値謫ｾB
	//   get_SignalStrength fVxPﾊの信ﾌ休l謫ｾB 
	int nStrength;
	int nQuality;
	int nLock;

	if (m_dwTargetChannel == CBonTuner::CHANNEL_INVALID)
		// SetChannel()xﾄばてなれ合0
		return 0.0F;

	GetSignalState(&nStrength, &nQuality, &nLock);
	if (!nLock)
		// Lockoﾄなれ合0
		return 0.0F;
	if (nStrength < 0 && m_bSignalLevelNeedStrength)
		// Strength-1ﾔれ合
		return (float)nStrength;

	m_fStrength = (double)nStrength;
	m_fQuality = (double)nQuality;

	try {
		f = (float)m_muParser.Eval();
	}
	catch (...) {
	}
	return f;
}

const DWORD CBonTuner::WaitTsStream(const DWORD dwTimeOut)
{
	if( m_hOnDecodeEvent == NULL ){
		return WAIT_ABANDONED;
	}

	DWORD dwRet;
	if (m_nWaitTsSleep) {
		// WaitTsSleep w閧ｳﾄれ合
		dwRet = ::WaitForSingleObject(m_hOnDecodeEvent, 0);
		// CxgVOiﾔでなば指闔榜待機
		if (dwRet != WAIT_TIMEOUT)
			return dwRet;

		::Sleep(m_nWaitTsSleep);
	}

	// CxgVOiﾔになの?
	dwRet = ::WaitForSingleObject(m_hOnDecodeEvent, (dwTimeOut)? dwTimeOut : INFINITE);
	return dwRet;
}

const DWORD CBonTuner::GetReadyCount(void)
{
	return (DWORD)m_DecodedTsBuff.Size();
}

const BOOL CBonTuner::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc = NULL;
	if (GetTsStream(&pSrc, pdwSize, pdwRemain)) {
		if (*pdwSize)
			memcpy(pDst, pSrc, *pdwSize);
		return TRUE;
	}
	return FALSE;
}

const BOOL CBonTuner::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	SAFE_DELETE(m_LastBuff);
	BOOL bRet = TRUE;
	m_LastBuff = m_DecodedTsBuff.Get();
	if (m_LastBuff) {
		*pdwSize = (DWORD)m_LastBuff->Size;
		*ppDst = m_LastBuff->pbyBuff;
		*pdwRemain = (DWORD)m_DecodedTsBuff.Size();
	}
	else {
		*pdwSize = 0;
		*ppDst = NULL;
		*pdwRemain = 0;
		bRet = FALSE;
	}
	return bRet;
}

void CBonTuner::PurgeTsStream(void)
{
	// m_LastBuff ﾍ参ﾆてつ能 delete 

	// MTSobt@
	m_TsBuff.Purge();

	// fR[hTSobt@
	m_DecodedTsBuff.Purge();

	// kpPbg尞彁bg
	::InterlockedExchange(&m_lResetFilter, 1);

	// rbg[gvZpNX
	m_BitRate.Clear();
}

LPCTSTR CBonTuner::GetTunerName(void)
{
	return m_aTunerParam.sTunerName.c_str();
}

const BOOL CBonTuner::IsTunerOpening(void)
{
	if (m_aCOMProc.hThread == NULL)
		return FALSE;

	DWORD dw;
	BOOL ret = FALSE;

	::EnterCriticalSection(&m_aCOMProc.csLock);

	m_aCOMProc.nRequest = enumCOMRequest::eCOMReqIsTunerOpening;
	::SetEvent(m_aCOMProc.hReqEvent);
	HANDLE h[2] = {
		m_aCOMProc.hEndEvent,
		m_aCOMProc.hThread
	};
	dw = ::WaitForMultipleObjects(2, h, FALSE, INFINITE);
	if (dw == WAIT_OBJECT_0) {
		ret = m_aCOMProc.uRetVal.IsTunerOpening;
	}

	::LeaveCriticalSection(&m_aCOMProc.csLock);
	return ret;
}

const BOOL CBonTuner::_IsTunerOpening(void)
{
	return m_bOpened;
}

LPCTSTR CBonTuner::EnumTuningSpace(const DWORD dwSpace)
{
	if (dwSpace < m_TuningData.dwNumSpace) {
		auto itSpace = m_TuningData.Spaces.find(dwSpace);
		if (itSpace != m_TuningData.Spaces.end())
			return itSpace->second.sTuningSpaceName.c_str();
		else
			return _T("-");
	}
	return NULL;
}

LPCTSTR CBonTuner::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	auto itSpace = m_TuningData.Spaces.find(dwSpace);
	if (itSpace != m_TuningData.Spaces.end()) {
		if (dwChannel < itSpace->second.dwNumChannel) {
			auto itCh = itSpace->second.Channels.find(dwChannel);
			if (itCh != itSpace->second.Channels.end())
				return itCh->second.sServiceName.c_str();
			else
				return _T("----");
		}
	}
	return NULL;
}

const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	if (m_aCOMProc.hThread == NULL)
		return FALSE;

	DWORD dw;
	BOOL ret = FALSE;

	::EnterCriticalSection(&m_aCOMProc.csLock);

	m_aCOMProc.nRequest = enumCOMRequest::eCOMReqSetChannel;
	m_aCOMProc.uParam.SetChannel.dwSpace = dwSpace;
	m_aCOMProc.uParam.SetChannel.dwChannel = dwChannel;
	::SetEvent(m_aCOMProc.hReqEvent);
	HANDLE h[2] = {
		m_aCOMProc.hEndEvent,
		m_aCOMProc.hThread
	};
	dw = ::WaitForMultipleObjects(2, h, FALSE, INFINITE);
	if (dw == WAIT_OBJECT_0) {
		ret = m_aCOMProc.uRetVal.SetChannel;
	}

	::LeaveCriticalSection(&m_aCOMProc.csLock);
	return ret;
}

static void PostTuneGvmvpazEnable(IBaseFilter *pFilter, DWORD requestedSid);

const BOOL CBonTuner::_SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	HRESULT hr;

	OutputDebug(L"SetChannel(%d, %d)\n", dwSpace, dwChannel);

	m_dwTargetSpace = m_dwCurSpace = CBonTuner::SPACE_INVALID;
	m_dwTargetChannel = m_dwCurChannel = CBonTuner::CHANNEL_INVALID;

	auto itSpace = m_TuningData.Spaces.find(dwSpace);
	if (itSpace == m_TuningData.Spaces.end()) {
		OutputDebug(L"    Invalid channel space.\n");
		return FALSE;
	}

	if (dwChannel >= itSpace->second.dwNumChannel) {
		OutputDebug(L"    Invalid channel number.\n");
		return FALSE;
	}

	auto itCh = itSpace->second.Channels.find(dwChannel);
	if (itCh == itSpace->second.Channels.end()) {
		OutputDebug(L"    Reserved channel number.\n");
		return FALSE;
	}

	if (!m_bOpened) {
		OutputDebug(L"    Tuner not opened.\n");
		return FALSE;
	}

	m_bRecvStarted = FALSE;
	PurgeTsStream();
	TuningSpaceData * TuningSpace = &itSpace->second;
	ChData * Ch = &itCh->second;
	m_LastTuningParam.Frequency = Ch->Frequency + TuningSpace->FrequencyOffset;					// g(MHz)
	m_LastTuningParam.Polarisation = PolarisationMapping[Ch->Polarisation];						// Mﾌ偏波
	m_LastTuningParam.Antenna = m_aSatellite[Ch->Satellite].Polarisation[Ch->Polarisation];		// Aeiﾝ断[^
	m_LastTuningParam.Modulation = m_aModulationType[Ch->ModulationType];						// ﾏ吊断[^
	m_LastTuningParam.ONID = Ch->ONID;															// IWilbg[NID / PhysicalChannel (ATSC / Digital Cable)
	m_LastTuningParam.TSID = Ch->TSID;															// gX|[gXg[ID / Channel (ATSC / Digital Cable)
	m_LastTuningParam.SID = Ch->SID;															// T[rXID / MinorChannel (ATSC / Digital Cable)
	m_LastTuningParam.MajorChannel = Ch->MajorChannel;											// MajorChannel (Digital Cable)
	m_LastTuningParam.SourceID = Ch->SourceID;													// SourceID (Digital Cable)
	m_LastTuningParam.IniSpaceID = dwSpace;														// init@Cﾅ読剤れた`[jOXy[X
	m_LastTuningParam.IniChannelID = dwChannel;													// init@Cﾅ読剤れた`l

	// IBdaSpecialsﾅ三ﾌ習vﾈＴ
	if (m_pIBdaSpecials2)
		hr = m_pIBdaSpecials2->PreLockChannel(&m_LastTuningParam);

	// Enter RUN state (with pre-Run HW config & default KSClock) BEFORE tuning, matching mAgicTV
	hr = RunGraph();
	OutputDebug(L"_SetChannel: RunGraph() before tune returned 0x%08X\n", hr);

	// Start BDA changes for devices requiring explicit transaction (e.g. GV-MVP/AZ)
	if (m_pTunerDevice) {
		CComQIPtr<IBDA_DeviceControl> pDevCtrl(m_pTunerDevice);
		if (pDevCtrl) {
			HRESULT hrSC = pDevCtrl->StartChanges();
			OutputDebug(L"  [BDA] StartChanges returned 0x%08X\n", hrSC);
		}
	}
	BOOL bRet = LockChannel(&m_LastTuningParam, m_bLockTwice && Ch->LockTwiceTarget);

	// IBdaSpecialsﾅ追歌習vﾈＴ
	if (m_pIBdaSpecials2)
		hr = m_pIBdaSpecials2->PostLockChannel(&m_LastTuningParam);

	SleepWithMessageLoop(100);
	PurgeTsStream();

	// TSMF
	switch (itSpace->second.TSMFMode) {
	case 0:		// OFF
		m_TSMFParser.Disable();
		m_bIsEnabledTSMF = FALSE;
		break;
	case 1:		// TSID
		m_TSMFParser.SetTSID((WORD)Ch->ONID, (WORD)Ch->TSID, FALSE);
		m_bIsEnabledTSMF = TRUE;
		break;
	case 2:		// Relative
		m_TSMFParser.SetTSID(0xffff, (WORD)Ch->TSID, TRUE);
		m_bIsEnabledTSMF = TRUE;
		break;
	}

	if (m_pTunerDevice) {
		PostTuneGvmvpazEnable(m_pTunerDevice, m_LastTuningParam.SID);
	}
	m_bRecvStarted = TRUE;

	// SetChannel()ﾝＡ[jOXy[Xﾔ哉チl
	m_dwTargetSpace = dwSpace;
	m_dwTargetChannel = dwChannel;

	if (bRet || m_bAlwaysAnswerLocked) {
		OutputDebug(L"SetChannel success.\n");
		m_dwCurSpace = dwSpace;
		m_dwCurChannel = dwChannel;
		return TRUE;
	}
	// m_byCurTone = CBonTuner::TONE_UNKNOWN;

	OutputDebug(L"SetChannel failed.\n");
	if (m_bAlwaysAnswerLocked)
		return TRUE;
	return FALSE;
}

const DWORD CBonTuner::GetCurSpace(void)
{
	if (m_aCOMProc.hThread == NULL)
		return CBonTuner::SPACE_INVALID;

	DWORD dw;
	DWORD ret = CBonTuner::SPACE_INVALID;

	::EnterCriticalSection(&m_aCOMProc.csLock);

	m_aCOMProc.nRequest = enumCOMRequest::eCOMReqGetCurSpace;
	::SetEvent(m_aCOMProc.hReqEvent);
	HANDLE h[2] = {
		m_aCOMProc.hEndEvent,
		m_aCOMProc.hThread
	};
	dw = ::WaitForMultipleObjects(2, h, FALSE, INFINITE);
	if (dw == WAIT_OBJECT_0) {
		ret = m_aCOMProc.uRetVal.GetCurSpace;
	}

	::LeaveCriticalSection(&m_aCOMProc.csLock);
	return ret;
}

const DWORD CBonTuner::_GetCurSpace(void)
{
	if (m_bAlwaysAnswerLocked)
		return m_dwTargetSpace;

	return m_dwCurSpace;
}

const DWORD CBonTuner::GetCurChannel(void)
{
	if (m_aCOMProc.hThread == NULL)
		return CBonTuner::CHANNEL_INVALID;

	DWORD dw;
	DWORD ret = CBonTuner::CHANNEL_INVALID;

	::EnterCriticalSection(&m_aCOMProc.csLock);

	m_aCOMProc.nRequest = enumCOMRequest::eCOMReqGetCurChannel;
	::SetEvent(m_aCOMProc.hReqEvent);
	HANDLE h[2] = {
		m_aCOMProc.hEndEvent,
		m_aCOMProc.hThread
	};
	dw = ::WaitForMultipleObjects(2, h, FALSE, INFINITE);
	if (dw == WAIT_OBJECT_0) {
		ret = m_aCOMProc.uRetVal.GetCurChannel;
	}

	::LeaveCriticalSection(&m_aCOMProc.csLock);
	return ret;
}

const DWORD CBonTuner::_GetCurChannel(void)
{
	if (m_bAlwaysAnswerLocked)
		return m_dwTargetChannel;

	return m_dwCurChannel;
}

void CBonTuner::Release(void)
{
	OutputDebug(L"CBonTuner::Release called.\n");

	delete this;
}

DWORD WINAPI CBonTuner::COMProcThread(LPVOID lpParameter)
{
	BOOL terminate = FALSE;
	CBonTuner* pSys = (CBonTuner*) lpParameter;
	COMProc* pCOMProc = &pSys->m_aCOMProc;
	HRESULT hr;

	OutputDebug(L"COMProcThread: Thread created.\n");

	// COM
	hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY);

	ULONGLONG lastAuthRefreshTick = 0;

	HANDLE h[2] = {
		pCOMProc->hTerminateRequest,
		pCOMProc->hReqEvent
	};

	while (!terminate) {
		DWORD ret = WaitForMultipleObjectsWithMessageLoop(2, h, FALSE, 1000);
		switch (ret)
		{
		case WAIT_OBJECT_0:
			terminate = TRUE;
			break;

		case WAIT_OBJECT_0 + 1:
			switch (pCOMProc->nRequest)
			{

			case eCOMReqOpenTuner:
				pCOMProc->uRetVal.OpenTuner = pSys->_OpenTuner();
				::SetEvent(pCOMProc->hEndEvent);
				break;

			case eCOMReqCloseTuner:
				// OpenTunerELockChannelﾌ再市ﾈら中~
				pCOMProc->ResetReOpenTuner();
				pCOMProc->ResetReLockChannel();

				pSys->_CloseTuner();
				::SetEvent(pCOMProc->hEndEvent);
				break;

			case eCOMReqSetChannel:
				// ﾙ常検mﾄ賛C}[
				pCOMProc->ResetWatchDog();

				// LockChannelﾌ再市ﾈら中~
				pCOMProc->ResetReLockChannel();

				// OpenTunerﾌ再市FALSE
				if (pCOMProc->bDoReOpenTuner) {
					pCOMProc->ClearReOpenChannel();
					pCOMProc->uRetVal.SetChannel = FALSE;
				}
				else {
					pCOMProc->uRetVal.SetChannel = pSys->_SetChannel(pCOMProc->uParam.SetChannel.dwSpace, pCOMProc->uParam.SetChannel.dwChannel);
				}
				::SetEvent(pCOMProc->hEndEvent);
				break;

			case eCOMReqGetSignalLevel:
				// OpenTunerﾌ再市0
				if (pCOMProc->bDoReOpenTuner) {
					pCOMProc->uRetVal.GetSignalLevel = 0.0F;
				}
				else {
					pCOMProc->uRetVal.GetSignalLevel = pSys->_GetSignalLevel();
				}
				::SetEvent(pCOMProc->hEndEvent);
				break;

			case eCOMReqIsTunerOpening:
				// OpenTunerﾌ再市ﾈ５RUE
				if (pCOMProc->bDoReOpenTuner) {
					pCOMProc->uRetVal.IsTunerOpening = TRUE;
				}
				else {
					pCOMProc->uRetVal.IsTunerOpening = pSys->_IsTunerOpening();
				}
				::SetEvent(pCOMProc->hEndEvent);
				break;

			case eCOMReqGetCurSpace:
				// OpenTunerﾌ再市ﾈむ罵
				if (pCOMProc->bDoReOpenTuner) {
					pCOMProc->uRetVal.GetCurSpace = pCOMProc->dwReOpenSpace;
				}
				else {
					pCOMProc->uRetVal.GetCurSpace = pSys->_GetCurSpace();
				}
				::SetEvent(pCOMProc->hEndEvent);
				break;

			case eCOMReqGetCurChannel:
				// OpenTunerﾌ再市ﾈむ罵
				if (pCOMProc->bDoReOpenTuner) {
					pCOMProc->uRetVal.GetCurChannel = pCOMProc->dwReOpenChannel;
				}
				else {
					pCOMProc->uRetVal.GetCurChannel = pSys->_GetCurChannel();
				}
				::SetEvent(pCOMProc->hEndEvent);
				break;

			default:
				break;
			}
			break;

		case WAIT_TIMEOUT:
			break;

		case WAIT_FAILED:
		default:
			DWORD err = ::GetLastError();
			OutputDebug(L"COMProcThread: Unknown error (ret=%d, LastError=0x%08x).\n", ret, err);
			terminate = TRUE;
			break;
		}

		if (terminate)
			break;

		// Xg[XbhvCIeBﾌ変更
		if (pSys->m_bIsSetStreamThread) {
			if (pSys->m_nThreadPriorityStream != THREAD_PRIORITY_ERROR_RETURN) {
				OutputDebug(L"COMProcThread: Current stream Thread priority = %d.\n", ::GetThreadPriority(pSys->m_hStreamThread));
				::SetThreadPriority(pSys->m_hStreamThread, pSys->m_nThreadPriorityStream);
				OutputDebug(L"COMProcThread: After changed stream Thread priority = %d.\n", ::GetThreadPriority(pSys->m_hStreamThread));
			}
			pSys->m_bIsSetStreamThread = FALSE;
		}

		// ﾙ常検mJo[
		// 1000ms
		if (pCOMProc->CheckTick()) {

			// Periodic GV-MVP/AZ auth refresh: gvmvpaz_x64.sys expires 5250E9E6 auth after 120s.
			if (pSys->m_pTunerDevice && pSys->m_bRecvStarted && !pCOMProc->bDoReOpenTuner) {
				ULONGLONG nowTick = ::GetTickCount64();
				if (lastAuthRefreshTick == 0) {
					lastAuthRefreshTick = nowTick;
				}
				else if (nowTick - lastAuthRefreshTick >= 30000ULL) {
					if (!pSys->RefreshGvmvpazAuth()) {
						OutputDebug(L"COMProcThread: Periodic RefreshGvmvpazAuth FAILED.\n");
					}
					lastAuthRefreshTick = nowTick;
				}
			}
			else {
				lastAuthRefreshTick = 0;
			}

			// SetChannel()sﾌバbNOhCHﾘ替開n
			if (pSys->m_bBackgroundChannelLock && pSys->m_dwCurChannel == CBonTuner::CHANNEL_INVALID && pSys->m_dwTargetChannel != CBonTuner::CHANNEL_INVALID) {
				OutputDebug(L"COMProcThread: Background retry.\n");
				pCOMProc->SetReLockChannel();
			}

			// ﾙ常検m
			if (!pCOMProc->bDoReLockChannel && !pCOMProc->bDoReOpenTuner && pSys->m_dwCurChannel != CBonTuner::CHANNEL_INVALID) {

				// SignalLockﾌ蕉確F
				if (pSys->m_nWatchDogSignalLocked != 0) {
					int lock = 0;
					pSys->GetSignalState(NULL, NULL, &lock);
					if (pCOMProc->CheckSignalLockErr(lock, pSys->m_nWatchDogSignalLocked * 1000)) {
						// `lbNﾄ市
						OutputDebug(L"COMProcThread: WatchDogSignalLocked time is up.\n");
						pCOMProc->SetReLockChannel();
					}
				} // SignalLockﾌ蕉確F

				// BitRatemF
				if (pSys->m_nWatchDogBitRate != 0) {
					if (pCOMProc->CheckBitRateErr((pSys->m_BitRate.GetRate() > 0.0), pSys->m_nWatchDogBitRate * 1000)) {
						// `lbNﾄ市
						OutputDebug(L"COMProcThread: WatchDogBitRate time is up.\n");
						pCOMProc->SetReLockChannel();
					}
				} // BitRatemF
			} // ﾙ常検m

			// CHﾘ替南試sOpenTunerﾄ市
			if (pCOMProc->bDoReOpenTuner) {
				// OpenTunerﾄ市
				pSys->_CloseTuner();
				if (pSys->_OpenTuner() && (!pCOMProc->CheckReOpenChannel() || pSys->_SetChannel(pCOMProc->dwReOpenSpace, pCOMProc->dwReOpenChannel))) {
					// OpenTunerﾉ植SetChannnelﾉ析必v
					OutputDebug(L"COMProcThread: Re-OpenTuner SUCCESS.\n");
					pCOMProc->ResetReOpenTuner();
				}
				else {
					// s...ﾌまま餐W
					OutputDebug(L"COMProcThread: Re-OpenTuner FAILED.\n");
				}
			} // CHﾘ替南試sOpenTunerﾄ市

			// ﾙ常検m`lbNﾄ市
			if (!pCOMProc->bDoReOpenTuner && pCOMProc->bDoReLockChannel) {
				// `lbNﾄ市
				if (pSys->LockChannel(&pSys->m_LastTuningParam, FALSE)) {
					// LockChannel
					OutputDebug(L"COMProcThread: Re-LockChannel SUCCESS.\n");
					pCOMProc->ResetReLockChannel();
				}
				else {
					// LockChannels
					OutputDebug(L"COMProcThread: Re-LockChannel FAILED.\n");
					if (pSys->m_nReOpenWhenGiveUpReLock != 0) {
						// CHﾘ替南試s�柏ﾝ値0ﾈ外
						if (pCOMProc->CheckReLockFailCount(pSys->m_nReOpenWhenGiveUpReLock)) {
							// CHﾘ替南試s�狽�ｴの０penTunerﾄ市
							OutputDebug(L"COMProcThread: ReOpenWhenGiveUpReLock count is up.\n");
							pCOMProc->SetReOpenTuner(pSys->m_dwTargetSpace, pSys->m_dwTargetChannel);
							pCOMProc->ResetReLockChannel();
						}
					}
				}
			} // ﾙ常検m`lbNﾄ市
		} // 1000ms
	} // while (!terminate)

	// Otﾖ係
	pSys->_CloseTuner();

	// DStB^[�唐ﾆチ[iELv`ﾌスg
	SAFE_DELETE(pSys->m_pDSFilterEnumTuner);
	SAFE_DELETE(pSys->m_pDSFilterEnumCapture);
	pSys->m_UsableTunerCaptureList.clear();

	::CoUninitialize();
	OutputDebug(L"COMProcThread: Thread terminated.\n");

	return 0;
}

DWORD WINAPI CBonTuner::DecodeProcThread(LPVOID lpParameter)
{
	BOOL terminate = FALSE;
	CBonTuner* pSys = (CBonTuner*)lpParameter;

	BOOL bNeedDecode = FALSE;

	HRESULT hr;

	OutputDebug(L"DecodeProcThread: Thread created.\n");

	// COM
	hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY);

	// IBdaSpecialsﾉＧR[hKv
	BOOL b = FALSE;
	if (pSys->m_pIBdaSpecials2 && SUCCEEDED(hr = pSys->m_pIBdaSpecials2->IsDecodingNeeded(&b))) {
		if (b)
			bNeedDecode = TRUE;
	}
	OutputDebug(L"DecodeProcThread: Detected IBdaSpecials decoding=%d.\n", bNeedDecode);

	HANDLE h[2] = {
		pSys->m_aDecodeProc.hTerminateRequest,
		pSys->m_hOnStreamEvent
	};

	while (!terminate) {
		DWORD remain = pSys->m_BitRate.CheckRate();
		DWORD ret = WaitForMultipleObjectsWithMessageLoop(2, h, FALSE, remain);
		switch (ret)
		{
		case WAIT_OBJECT_0:
			terminate = TRUE;
			break;
		case WAIT_OBJECT_0 + 1:
			{
				// TSobt@ﾌデ[^謫ｾ
				while (TS_DATA *pBuff = pSys->m_TsBuff.Get()) {
					// KvIBdaSpecialsﾉＧR[hs
					if (bNeedDecode) {
						pSys->m_pIBdaSpecials2->Decode(pBuff->pbyBuff, (DWORD)pBuff->Size);
					}

					// 謫ｾobt@fR[hﾏみバbt@ﾉ?
					if (pSys->m_bIsEnabledTSMF) {
						// TSMFﾌ峻
						BYTE * newBuf = NULL;
						size_t newBufSize = 0;
						pSys->m_TSMFParser.ParseTsBuffer(pBuff->pbyBuff, pBuff->Size, &newBuf, &newBufSize, pSys->m_bDeleteNullPackets);
						if (newBuf) {
							TS_DATA * pNewTS = new TS_DATA(newBuf, (DWORD)newBufSize, FALSE);
							pSys->m_DecodedTsBuff.Add(pNewTS);
						}
						SAFE_DELETE(pBuff);
					}
					else if (pSys->m_bDeleteNullPackets) {
						if (::InterlockedExchange(&pSys->m_lResetFilter, 0)) {
							// Zbg
							pSys->m_PacketSize = 0;
							pSys->m_FilterBuf.clear();
						}

						// {Iﾉ取得f[^ﾌξ芋利用驍ｱﾆでノﾛな鈴が
						// [~ﾏとＨ
						size_t sizeToExtend = pSys->m_FilterBuf.size() >= 188 * 16 ? pBuff->Size + 188 * 16 : 0;
						pSys->m_FilterBuf.insert(pSys->m_FilterBuf.end(), pBuff->pbyBuff, pBuff->pbyBuff + pBuff->Size);
						if (sizeToExtend > 0) {
							// g
							SAFE_DELETE(pBuff);
							pBuff = new TS_DATA(new BYTE[sizeToExtend], sizeToExtend, FALSE);
						}

						size_t rpos = 0;
						size_t wpos = 0;
						while (rpos + pSys->m_PacketSize <= pSys->m_FilterBuf.size() && wpos + 188 <= pBuff->Size) {
							if (pSys->m_PacketSize == 0) {
								// TSpPbg
								size_t truncate = 0;
								CTSMFParser::SyncPacket(pSys->m_FilterBuf.data() + rpos, pSys->m_FilterBuf.size() - rpos, &truncate, &pSys->m_PacketSize);
								rpos += truncate;
								if (truncate == 0)
									// TSobt@ﾌデ[^TCYﾄ毒?
									break;
							}
							else if (pSys->m_FilterBuf[rpos] != CTSMFParser::TS_PACKET_SYNC_BYTE) {
								// O
								pSys->m_PacketSize = 0;
							}
							else {
								WORD pid = ((pSys->m_FilterBuf[rpos + 1] << 8) | pSys->m_FilterBuf[rpos + 2]) & 0x1fff;
								if (pid != 0x1fff) {
									// kpPbgﾅなので?
									memcpy(pBuff->pbyBuff + wpos, pSys->m_FilterBuf.data() + rpos, 188);
									wpos += 188;
								}
								rpos += pSys->m_PacketSize;
							}
						}

						pSys->m_FilterBuf.erase(pSys->m_FilterBuf.begin(), pSys->m_FilterBuf.begin() + rpos);
						if (wpos > 0) {
							pBuff->Size = wpos;
							pSys->m_DecodedTsBuff.Add(pBuff);
						}
						else {
							SAFE_DELETE(pBuff);
						}
					}
					else {
						// TSMFﾌ峻ﾈれ合ﾍのまま?
						pSys->m_DecodedTsBuff.Add(pBuff);
					}

					// MCxgZbg
					if (pSys->m_DecodedTsBuff.Size() >= pSys->m_nWaitTsCount)
						::SetEvent(pSys->m_hOnDecodeEvent);
				}
			}
			break;
		case WAIT_TIMEOUT:
			break;
		case WAIT_FAILED:
		default:
			DWORD err = ::GetLastError();
			OutputDebug(L"DecodeProcThread: Unknown error (ret=%d, LastError=0x%08x).\n", ret, err);
			terminate = TRUE;
			break;
		}
	}

	::CoUninitialize();
	OutputDebug(L"DecodeProcThread: Thread terminated.\n");

	return 0;
}

static void EnsureCs(CRITICAL_SECTION &cs, volatile LONG &state)
{
	if (InterlockedCompareExchange(&state, 1, 0) == 0) {
		InitializeCriticalSection(&cs);
		InterlockedExchange(&state, 2);
	}
	while (InterlockedCompareExchange(&state, 2, 2) != 2) {
		Sleep(0);
	}
}

static BCRYPT_ALG_HANDLE g_aesAlg = NULL;
static BCRYPT_KEY_HANDLE g_aesKey = NULL;
static volatile LONG g_aesReady = 0;
static CRITICAL_SECTION g_aesCs;
static volatile LONG g_aesCsInit = 0;

static void EnsureAesCs()
{
	EnsureCs(g_aesCs, g_aesCsInit);
}

static void SetGvmvpazSessionKey(const BYTE key16[16])
{
	EnsureAesCs();
	EnterCriticalSection(&g_aesCs);
	InterlockedExchange(&g_aesReady, 0);
	if (g_aesKey) {
		BCryptDestroyKey(g_aesKey);
		g_aesKey = NULL;
	}
	if (!g_aesAlg) {
		NTSTATUS st = BCryptOpenAlgorithmProvider(&g_aesAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
		if (st >= 0) {
			BCryptSetProperty(g_aesAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
		}
	}
	if (g_aesAlg) {
		NTSTATUS st = BCryptGenerateSymmetricKey(g_aesAlg, &g_aesKey, NULL, 0, (PUCHAR)key16, 16, 0);
		if (st >= 0 && g_aesKey) {
			InterlockedExchange(&g_aesReady, 1);
			OutputDebug(L"  [PreRun] AES-128 transport key armed: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				key16[0], key16[1], key16[2], key16[3], key16[4], key16[5], key16[6], key16[7],
				key16[8], key16[9], key16[10], key16[11], key16[12], key16[13], key16[14], key16[15]);
		} else {
			OutputDebug(L"  [PreRun] BCryptGenerateSymmetricKey failed: 0x%08X\n", (unsigned int)st);
		}
	}
	LeaveCriticalSection(&g_aesCs);
}

static void DecryptGvmvpazPacket(BYTE pkt[188])
{
	static const BYTE kIv0[16] = {
		0xEC, 0x8F, 0x4B, 0x6A, 0xD9, 0x2A, 0x36, 0x89,
		0x2B, 0xDF, 0xB6, 0x18, 0xFC, 0x25, 0x5E, 0xFC
	};
	if ((pkt[3] & 0xC0) == 0 || InterlockedCompareExchange(&g_aesReady, 1, 1) != 1)
		return;
	BYTE afc = pkt[3] & 0x30;
	size_t payOff = 0;
	if (afc == 0x10) {
		payOff = 4;
	} else if (afc == 0x30) {
		payOff = 5 + (size_t)pkt[4];
	} else {
		return;
	}
	if (payOff >= 188)
		return;

	size_t payLen = 188 - payOff;
	size_t nBlocks = payLen / 16;
	size_t rem = payLen % 16;
	BYTE *pay = pkt + payOff;

	BYTE lastCt[16];
	if (nBlocks > 0) {
		memcpy(lastCt, pay + (nBlocks - 1) * 16, 16);
	} else {
		memcpy(lastCt, kIv0, 16);
	}

	EnsureAesCs();
	EnterCriticalSection(&g_aesCs);
	if (g_aesKey) {
		ULONG cbOut = 0;
		if (nBlocks > 0) {
			BYTE ivCbc[16];
			memcpy(ivCbc, kIv0, 16);
			BCryptDecrypt(g_aesKey, pay, (ULONG)(nBlocks * 16), NULL, ivCbc, 16, pay, (ULONG)(nBlocks * 16), &cbOut, 0);
		}
		if (rem > 0) {
			BYTE ivZero[16] = { 0 };
			BYTE ks[16] = { 0 };
			BCryptEncrypt(g_aesKey, lastCt, 16, NULL, ivZero, 16, ks, 16, &cbOut, 0);
			BYTE *tail = pay + nBlocks * 16;
			for (size_t i = 0; i < rem; i++) {
				tail[i] ^= ks[i];
			}
		}
		pkt[3] &= 0x3F;
	}
	LeaveCriticalSection(&g_aesCs);
}

struct PsiSectionAssembler {
	WORD targetPid;
	BYTE targetTableId;
	WORD targetExtId; // 0xFFFF = wildcard
	BYTE sec[1024];
	size_t expected;
	size_t len;
	BYTE cc;
	volatile LONG ready; // 0 = capturing, 1 = ready, 2 = idle
};

static CRITICAL_SECTION g_psiCs;
static volatile LONG g_psiCsInit = 0;
static PsiSectionAssembler g_patAsm = { 0x0000, 0x00, 0xFFFF, {}, 0, 0, 0xFF, 2 };
static PsiSectionAssembler g_pmtAsm = { 0xFFFF, 0x02, 0xFFFF, {}, 0, 0, 0xFF, 2 };

static void ResetPsiAssembler(PsiSectionAssembler &a, WORD pid, BYTE tableId, WORD extId, LONG initialState)
{
	EnsureCs(g_psiCs, g_psiCsInit);
	EnterCriticalSection(&g_psiCs);
	InterlockedExchange(&a.ready, initialState);
	a.targetPid = pid;
	a.targetTableId = tableId;
	a.targetExtId = extId;
	a.expected = 0;
	a.len = 0;
	a.cc = 0xFF;
	LeaveCriticalSection(&g_psiCs);
}

static void AccumulatePsiSection(PsiSectionAssembler &a, WORD pid, const BYTE *pkt)
{
	if (pid != a.targetPid || a.ready != 0) return;
	BOOL pusi = (pkt[1] & 0x40) != 0;
	BYTE afc = (pkt[3] >> 4) & 3;
	BYTE cc = pkt[3] & 0x0F;
	if ((afc & 1) == 0) return;
	size_t pay = 4 + ((afc >= 2) ? (1 + (size_t)pkt[4]) : 0);
	if (pay >= 188) return;

	if (pusi) {
		size_t start = pay + 1 + (size_t)pkt[pay];
		if (start + 8 <= 188 && pkt[start] == a.targetTableId) {
			WORD extId = ((WORD)pkt[start + 3] << 8) | pkt[start + 4];
			if (a.targetExtId == 0xFFFF || extId == a.targetExtId) {
				size_t secLen = ((size_t)(pkt[start + 1] & 0x0F) << 8) | pkt[start + 2];
				size_t total = 3 + secLen;
				if (total >= 12 && total <= sizeof(a.sec)) {
					size_t copyLen = 188 - start;
					if (copyLen > total) copyLen = total;
					memcpy(a.sec, pkt + start, copyLen);
					a.expected = total;
					a.len = copyLen;
					a.cc = cc;
					if (a.len >= a.expected) {
						InterlockedExchange(&a.ready, 1);
					}
				}
			}
		}
	} else if (a.expected > 0 && a.len < a.expected && ((a.cc + 1) & 0x0F) == cc) {
		size_t rem = a.expected - a.len;
		size_t copyLen = 188 - pay;
		if (copyLen > rem) copyLen = rem;
		memcpy(a.sec + a.len, pkt + pay, copyLen);
		a.len += copyLen;
		a.cc = cc;
		if (a.len >= a.expected) {
			InterlockedExchange(&a.ready, 1);
		}
	}
}

int CALLBACK CBonTuner::RecvProc(void* pParam, BYTE* pbData, size_t size)
{
	CBonTuner* pSys = (CBonTuner*)pParam;
	if (pbData && size >= 188) {
		bool needPat = (InterlockedCompareExchange(&g_patAsm.ready, 0, 0) == 0);
		bool needPmt = (InterlockedCompareExchange(&g_pmtAsm.ready, 0, 0) == 0);
		for (size_t off = 0; off + 188 <= size; off += 188) {
			BYTE *pkt = pbData + off;
			if (pkt[0] != 0x47) continue;
			if ((pkt[3] & 0xC0) != 0) {
				DecryptGvmvpazPacket(pkt);
			}
			if (needPat || needPmt) {
				WORD pid = (WORD)(((pkt[1] & 0x1F) << 8) | pkt[2]);
				EnsureCs(g_psiCs, g_psiCsInit);
				EnterCriticalSection(&g_psiCs);
				if (needPat) {
					AccumulatePsiSection(g_patAsm, pid, pkt);
					needPat = (g_patAsm.ready == 0);
				}
				if (needPmt) {
					AccumulatePsiSection(g_pmtAsm, pid, pkt);
					needPmt = (g_pmtAsm.ready == 0);
				}
				LeaveCriticalSection(&g_psiCs);
			}
		}
	}

	if (pSys->m_hStreamThread == NULL) {
		::DuplicateHandle(pSys->m_hProcess, GetCurrentThread(), GetCurrentProcess(), &(pSys->m_hStreamThread), 0, FALSE, 2);
		pSys->m_bIsSetStreamThread = TRUE;
	}

	pSys->m_BitRate.AddRate((DWORD)size);

	if (pSys->m_bRecvStarted) {
		if (pSys->m_TsBuff.AddData(pbData, size)) {
			::SetEvent(pSys->m_hOnStreamEvent);
		}
	}

	return 0;
}

void CBonTuner::StartRecv(void)
{
	OutputDebug(L"  [StartRecv] m_pITsWriter=%p\n", m_pITsWriter.p);
	if (m_pITsWriter)
		m_pITsWriter->SetCallBackRecv(RecvProc, this);
	m_bRecvStarted = TRUE;
}

void CBonTuner::StopRecv(void)
{
	if (m_pITsWriter)
		m_pITsWriter->SetCallBackRecv(NULL, this);
	m_bRecvStarted = FALSE;
}

void CBonTuner::ReadIniFile(void)
{
	const std::map<const std::wstring, const int, std::less<>> mapThreadPriority = {
		{ L"",                              THREAD_PRIORITY_ERROR_RETURN },
		{ L"THREAD_PRIORITY_IDLE",          THREAD_PRIORITY_IDLE },
		{ L"THREAD_PRIORITY_LOWEST",        THREAD_PRIORITY_LOWEST },
		{ L"THREAD_PRIORITY_BELOW_NORMAL",  THREAD_PRIORITY_BELOW_NORMAL },
		{ L"THREAD_PRIORITY_NORMAL",        THREAD_PRIORITY_NORMAL },
		{ L"THREAD_PRIORITY_ABOVE_NORMAL",  THREAD_PRIORITY_ABOVE_NORMAL },
		{ L"THREAD_PRIORITY_HIGHEST",       THREAD_PRIORITY_HIGHEST },
		{ L"THREAD_PRIORITY_TIME_CRITICAL", THREAD_PRIORITY_TIME_CRITICAL },
	};

	const std::map<const std::wstring, const int, std::less<>> mapModulationType = {
		{ L"BDA_MOD_NOT_SET",          ModulationType::BDA_MOD_NOT_SET },
		{ L"BDA_MOD_NOT_DEFINED",      ModulationType::BDA_MOD_NOT_DEFINED },
		{ L"BDA_MOD_16QAM",            ModulationType::BDA_MOD_16QAM },
		{ L"BDA_MOD_32QAM",            ModulationType::BDA_MOD_32QAM },
		{ L"BDA_MOD_64QAM",            ModulationType::BDA_MOD_64QAM },
		{ L"BDA_MOD_80QAM",            ModulationType::BDA_MOD_80QAM },
		{ L"BDA_MOD_96QAM",            ModulationType::BDA_MOD_96QAM },
		{ L"BDA_MOD_112QAM",           ModulationType::BDA_MOD_112QAM },
		{ L"BDA_MOD_128QAM",           ModulationType::BDA_MOD_128QAM },
		{ L"BDA_MOD_160QAM",           ModulationType::BDA_MOD_160QAM },
		{ L"BDA_MOD_192QAM",           ModulationType::BDA_MOD_192QAM },
		{ L"BDA_MOD_224QAM",           ModulationType::BDA_MOD_224QAM },
		{ L"BDA_MOD_256QAM",           ModulationType::BDA_MOD_256QAM },
		{ L"BDA_MOD_320QAM",           ModulationType::BDA_MOD_320QAM },
		{ L"BDA_MOD_384QAM",           ModulationType::BDA_MOD_384QAM },
		{ L"BDA_MOD_448QAM",           ModulationType::BDA_MOD_448QAM },
		{ L"BDA_MOD_512QAM",           ModulationType::BDA_MOD_512QAM },
		{ L"BDA_MOD_640QAM",           ModulationType::BDA_MOD_640QAM },
		{ L"BDA_MOD_768QAM",           ModulationType::BDA_MOD_768QAM },
		{ L"BDA_MOD_896QAM",           ModulationType::BDA_MOD_896QAM },
		{ L"BDA_MOD_1024QAM",          ModulationType::BDA_MOD_1024QAM },
		{ L"BDA_MOD_QPSK",             ModulationType::BDA_MOD_QPSK },
		{ L"BDA_MOD_BPSK",             ModulationType::BDA_MOD_BPSK },
		{ L"BDA_MOD_OQPSK",            ModulationType::BDA_MOD_OQPSK },
		{ L"BDA_MOD_8VSB",             ModulationType::BDA_MOD_8VSB },
		{ L"BDA_MOD_16VSB",            ModulationType::BDA_MOD_16VSB },
		{ L"BDA_MOD_ANALOG_AMPLITUDE", ModulationType::BDA_MOD_ANALOG_AMPLITUDE },
		{ L"BDA_MOD_ANALOG_FREQUENCY", ModulationType::BDA_MOD_ANALOG_FREQUENCY },
		{ L"BDA_MOD_8PSK",             ModulationType::BDA_MOD_8PSK },
		{ L"BDA_MOD_RF",               ModulationType::BDA_MOD_RF },
		{ L"BDA_MOD_16APSK",           ModulationType::BDA_MOD_16APSK },
		{ L"BDA_MOD_32APSK",           ModulationType::BDA_MOD_32APSK },
		{ L"BDA_MOD_NBC_QPSK",         ModulationType::BDA_MOD_NBC_QPSK },
		{ L"BDA_MOD_NBC_8PSK",         ModulationType::BDA_MOD_NBC_8PSK },
		{ L"BDA_MOD_DIRECTV",          ModulationType::BDA_MOD_DIRECTV },
		{ L"BDA_MOD_ISDB_T_TMCC",      ModulationType::BDA_MOD_ISDB_T_TMCC },
		{ L"BDA_MOD_ISDB_S_TMCC",      ModulationType::BDA_MOD_ISDB_S_TMCC },
	};

	const std::map<const std::wstring, const int, std::less<>> mapFECMethod = {
		{ L"BDA_FEC_METHOD_NOT_SET",     FECMethod::BDA_FEC_METHOD_NOT_SET },
		{ L"BDA_FEC_METHOD_NOT_DEFINED", FECMethod::BDA_FEC_METHOD_NOT_DEFINED },
		{ L"BDA_FEC_VITERBI",            FECMethod::BDA_FEC_VITERBI },
		{ L"BDA_FEC_RS_204_188",         FECMethod::BDA_FEC_RS_204_188 },
		{ L"BDA_FEC_LDPC",               FECMethod::BDA_FEC_LDPC },
		{ L"BDA_FEC_BCH",                FECMethod::BDA_FEC_BCH },
		{ L"BDA_FEC_RS_147_130",         FECMethod::BDA_FEC_RS_147_130 },
	};

	const std::map<const std::wstring, const int, std::less<>> mapBinaryConvolutionCodeRate = {
		{ L"BDA_BCC_RATE_NOT_SET",     BinaryConvolutionCodeRate::BDA_BCC_RATE_NOT_SET },
		{ L"BDA_BCC_RATE_NOT_DEFINED", BinaryConvolutionCodeRate::BDA_BCC_RATE_NOT_DEFINED },
		{ L"BDA_BCC_RATE_1_2",         BinaryConvolutionCodeRate::BDA_BCC_RATE_1_2 },
		{ L"BDA_BCC_RATE_2_3",         BinaryConvolutionCodeRate::BDA_BCC_RATE_2_3 },
		{ L"BDA_BCC_RATE_3_4",         BinaryConvolutionCodeRate::BDA_BCC_RATE_3_4 },
		{ L"BDA_BCC_RATE_3_5",         BinaryConvolutionCodeRate::BDA_BCC_RATE_3_5 },
		{ L"BDA_BCC_RATE_4_5",         BinaryConvolutionCodeRate::BDA_BCC_RATE_4_5 },
		{ L"BDA_BCC_RATE_5_6",         BinaryConvolutionCodeRate::BDA_BCC_RATE_5_6 },
		{ L"BDA_BCC_RATE_5_11",        BinaryConvolutionCodeRate::BDA_BCC_RATE_5_11 },
		{ L"BDA_BCC_RATE_7_8",         BinaryConvolutionCodeRate::BDA_BCC_RATE_7_8 },
		{ L"BDA_BCC_RATE_1_4",         BinaryConvolutionCodeRate::BDA_BCC_RATE_1_4 },
		{ L"BDA_BCC_RATE_1_3",         BinaryConvolutionCodeRate::BDA_BCC_RATE_1_3 },
		{ L"BDA_BCC_RATE_2_5",         BinaryConvolutionCodeRate::BDA_BCC_RATE_2_5 },
		{ L"BDA_BCC_RATE_6_7",         BinaryConvolutionCodeRate::BDA_BCC_RATE_6_7 },
		{ L"BDA_BCC_RATE_8_9",         BinaryConvolutionCodeRate::BDA_BCC_RATE_8_9 },
		{ L"BDA_BCC_RATE_9_10",        BinaryConvolutionCodeRate::BDA_BCC_RATE_9_10 },
	};

	const std::map<const std::wstring, const int, std::less<>> mapTuningSpaceType = {
		{ L"DVB-S/DVB-S2",  enumTunerType::eTunerTypeDVBS },
		{ L"DVB-S2",        enumTunerType::eTunerTypeDVBS },
		{ L"DVB-S",         enumTunerType::eTunerTypeDVBS },
		{ L"DVB-T",         enumTunerType::eTunerTypeDVBT },
		{ L"DVB-C",         enumTunerType::eTunerTypeDVBC },
		{ L"DVB-T2",        enumTunerType::eTunerTypeDVBT2 },
		{ L"ISDB-S",        enumTunerType::eTunerTypeISDBS },
		{ L"ISDB-T",        enumTunerType::eTunerTypeISDBT },
		{ L"ISDB-C",        enumTunerType::eTunerTypeISDBC },
		{ L"ATSC",          enumTunerType::eTunerTypeATSC_Antenna },
		{ L"ATSC CABLE",    enumTunerType::eTunerTypeATSC_Cable },
		{ L"DIGITAL CABLE", enumTunerType::eTunerTypeDigitalCable },
	};

	const std::map<const std::wstring, const int, std::less<>> mapSpecifyTuningSpace = {
		{ L"AUTO",                     enumTuningSpace::eTuningSpaceAuto },
		{ L"DVBTUNINGSPACE",           enumTuningSpace::eTuningSpaceDVB },
		{ L"DVBSTUNINGSPACE",          enumTuningSpace::eTuningSpaceDVBS },
		{ L"ANALOGTVTUNINGSPACE",      enumTuningSpace::eTuningSpaceAnalogTV },
		{ L"ATSCTUNINGSPACE",	       enumTuningSpace::eTuningSpaceATSC },
		{ L"DIGITALCABLETUNINGSPACE",  enumTuningSpace::eTuningSpaceDigitalCable },
	};

	const std::map<const std::wstring, const int, std::less<>> mapSpecifyLocator = {
		{ L"AUTO", eLocatorAuto },
		{ L"DVBTLOCATOR",         enumLocator::eLocatorDVBT },
		{ L"DVBTLOCATOR2",        enumLocator::eLocatorDVBT2 },
		{ L"DVBSLOCATOR",         enumLocator::eLocatorDVBS },
		{ L"DVBCLOCATOR",         enumLocator::eLocatorDVBC },
		{ L"ISDBSLOCATOR",        enumLocator::eLocatorISDBS },
		{ L"ATSCLOCATOR",         enumLocator::eLocatorATSC },
		{ L"DIGITALCABLELOCATOR", enumLocator::eLocatorDigitalCable },
	};

	const std::map<const std::wstring, const int, std::less<>> mapSpecifyITuningSpaceNetworkType = {
		{ L"AUTO",                                       enumNetworkType::eNetworkTypeAuto },
		{ L"STATIC_DVB_TERRESTRIAL_TV_NETWORK_TYPE",     enumNetworkType::eNetworkTypeDVBT },
		{ L"STATIC_DVB_SATELLITE_TV_NETWORK_TYPE",       enumNetworkType::eNetworkTypeDVBS },
		{ L"STATIC_DVB_CABLE_TV_NETWORK_TYPE",           enumNetworkType::eNetworkTypeDVBC },
		{ L"STATIC_ISDB_TERRESTRIAL_TV_NETWORK_TYPE",    enumNetworkType::eNetworkTypeISDBT },
		{ L"STATIC_ISDB_SATELLITE_TV_NETWORK_TYPE",      enumNetworkType::eNetworkTypeISDBS },
		{ L"STATIC_ISDB_CABLE_TV_NETWORK_TYPE",          enumNetworkType::eNetworkTypeISDBC },
		{ L"STATIC_ATSC_TERRESTRIAL_TV_NETWORK_TYPE",    enumNetworkType::eNetworkTypeATSC },
		{ L"STATIC_DIGITAL_CABLE_NETWORK_TYPE",          enumNetworkType::eNetworkTypeDigitalCable },
		{ L"STATIC_BSKYB_TERRESTRIAL_TV_NETWORK_TYPE",   enumNetworkType::eNetworkTypeBSkyB },
		{ L"STATIC_DIRECT_TV_SATELLITE_TV_NETWORK_TYPE", enumNetworkType::eNetworkTypeDIRECTV },
		{ L"STATIC_ECHOSTAR_SATELLITE_TV_NETWORK_TYPE",  enumNetworkType::eNetworkTypeEchoStar },
	};

	const std::map<const std::wstring, const int, std::less<>> mapSpecifyIDVBTuningSpaceSystemType = {
		{ L"AUTO",             enumDVBSystemType::eDVBSystemTypeAuto },
		{ L"DVB_CABLE",        enumDVBSystemType::eDVBSystemTypeDVBC },
		{ L"DVB_TERRESTRIAL",  enumDVBSystemType::eDVBSystemTypeDVBT },
		{ L"DVB_SATELLITE",    enumDVBSystemType::eDVBSystemTypeDVBS },
		{ L"ISDB_TERRESTRIAL", enumDVBSystemType::eDVBSystemTypeISDBT },
		{ L"ISDB_SATELLITE",   enumDVBSystemType::eDVBSystemTypeISDBS },
	};


	const std::map<const std::wstring, const int, std::less<>> mapSpecifyIAnalogTVTuningSpaceInputType = {
		{ L"AUTO",              enumTunerInputType::eTunerInputTypeAuto },
		{ L"TUNERINPUTCABLE",   enumTunerInputType::eTunerInputTypeCable },
		{ L"TUNERINPUTANTENNA", enumTunerInputType::eTunerInputTypeAntenna },
	};

	const std::map<const std::wstring, const int, std::less<>> mapNetworkProvider = {
		{ L"AUTO",                             enumNetworkProvider::eNetworkProviderAuto },
		{ L"MICROSOFT NETWORK PROVIDER",       enumNetworkProvider::eNetworkProviderGeneric },
		{ L"MICROSOFT DVB-S NETWORK PROVIDER", enumNetworkProvider::eNetworkProviderDVBS },
		{ L"MICROSOFT DVB-T NETWORK PROVIDER", enumNetworkProvider::eNetworkProviderDVBT },
		{ L"MICROSOFT DVB-C NETWORK PROVIDER", enumNetworkProvider::eNetworkProviderDVBC },
		{ L"MICROSOFT ATSC NETWORK PROVIDER",  enumNetworkProvider::eNetworkProviderATSC },
	};

	const std::map<const std::wstring, const int, std::less<>> mapDefaultNetwork = {
		{ L"NONE",     enumDefaultNetwork::eDefaultNetworkNone },
		{ L"SPHD",     enumDefaultNetwork::eDefaultNetworkSPHD },
		{ L"BS/CS110", enumDefaultNetwork::eDefaultNetworkBSCS },
		{ L"BS",       enumDefaultNetwork::eDefaultNetworkBSCS },
		{ L"CS110",    enumDefaultNetwork::eDefaultNetworkBSCS },
		{ L"UHF/CATV", enumDefaultNetwork::eDefaultNetworkUHF },
		{ L"UHF",      enumDefaultNetwork::eDefaultNetworkUHF },
		{ L"CATV",     enumDefaultNetwork::eDefaultNetworkUHF },
		{ L"DUAL",     enumDefaultNetwork::eDefaultNetworkDual },
	};

	const std::map<const std::wstring, const int, std::less<>> mapSignalLevelCalcType = {
		{ L"SSSTRENGTH",      enumSignalLevelCalcType::eSignalLevelCalcTypeSSStrength },
		{ L"SSQUALITY",       enumSignalLevelCalcType::eSignalLevelCalcTypeSSQuality },
		{ L"SSMUL",           enumSignalLevelCalcType::eSignalLevelCalcTypeSSMul },
		{ L"SSADD",           enumSignalLevelCalcType::eSignalLevelCalcTypeSSAdd },
		{ L"SSFORMULA",       enumSignalLevelCalcType::eSignalLevelCalcTypeSSFormula },
		{ L"TUNERSTRENGTH",   enumSignalLevelCalcType::eSignalLevelCalcTypeTunerStrength },
		{ L"TUNERQUALITY",    enumSignalLevelCalcType::eSignalLevelCalcTypeTunerQuality },
		{ L"TUNERMUL",        enumSignalLevelCalcType::eSignalLevelCalcTypeTunerMul },
		{ L"TUNERADD",        enumSignalLevelCalcType::eSignalLevelCalcTypeTunerAdd },
		{ L"TUNERFORMULA",    enumSignalLevelCalcType::eSignalLevelCalcTypeTunerFormula },
		{ L"DEMODSSSTRENGTH", enumSignalLevelCalcType::eSignalLevelCalcTypeDemodSSStrength },
		{ L"DEMODSSQUALITY",  enumSignalLevelCalcType::eSignalLevelCalcTypeDemodSSQuality },
		{ L"DEMODSSMUL",      enumSignalLevelCalcType::eSignalLevelCalcTypeDemodSSMul },
		{ L"DEMODSSADD",      enumSignalLevelCalcType::eSignalLevelCalcTypeDemodSSAdd },
		{ L"DEMODSSFORMULA",  enumSignalLevelCalcType::eSignalLevelCalcTypeDemodSSFormula },
		{ L"BITRATE",         enumSignalLevelCalcType::eSignalLevelCalcTypeBR },
	};

	const std::map<const std::wstring, const int, std::less<>> mapSignalLockedJudgeType = {
		{ L"ALWAYS",        enumSignalLockedJudgeType::eSignalLockedJudgeTypeAlways },
		{ L"SSLOCKED",      enumSignalLockedJudgeType::eSignalLockedJudgeTypeSS },
		{ L"TUNERSTRENGTH", enumSignalLockedJudgeType::eSignalLockedJudgeTypeTuner },
		{ L"DEMODSSLOCKED", enumSignalLockedJudgeType::eSignalLockedJudgeTypeDemodSS },
	};

	const std::map<const std::wstring, const int, std::less<>> mapDiSEqC = {
		{ L"",       LNB_Source::BDA_LNB_SOURCE_NOT_SET },
		{ L"PORT-A", LNB_Source::BDA_LNB_SOURCE_A },
		{ L"PORT-B", LNB_Source::BDA_LNB_SOURCE_B },
		{ L"PORT-C", LNB_Source::BDA_LNB_SOURCE_C },
		{ L"PORT-D", LNB_Source::BDA_LNB_SOURCE_D },
	};

	const std::map<const std::wstring, const int, std::less<>> mapTSMFMode = {
		{ L"OFF",      0 },
		{ L"TSID",     1 },
		{ L"RELATIVE", 2 },
	};

	// INIt@Cﾌフ@C謫ｾ
	std::wstring tempPath = common::GetModuleName(st_hModule);
	m_sIniFilePath = tempPath + L"ini";

	CIniFileAccess IniFileAccess(m_sIniFilePath);
	int val;

	// DebugLogL^驍ｩ
	if (IniFileAccess.ReadKeyB(L"BONDRIVER", L"DebugLog", FALSE)) {
		SetDebugLog(tempPath + L"log");
	}

	//
	// Tuner ZNV
	//
	IniFileAccess.ReadSection(L"TUNER");
	IniFileAccess.CreateSectionData();

	// GUID0 - GUID99: TunerfoCXGUID ... w閧ｳﾈば個に使ﾓ烹
	// FriendlyName0 - FriendlyName99: TunerfoCXFriendlyName ... w閧ｳﾈば個に使ﾓ烹
	// CaptureGUID0 - CaptureGUID99: CapturefoCXGUID ... w閧ｳﾈば接堕能ﾈデoCXB
	// CaptureFriendlyName0 - CaptureFriendlyName99: CapturefoCXFriendlyName ... w閧ｳﾈば接堕能ﾈデoCXB
	for (unsigned int i = 0; i < MAX_GUID; i++) {
		std::wstring key;
		key = L"GUID" + std::to_wstring(i);
		std::wstring tunerGuid = IniFileAccess.ReadKeySSectionData(key, L"");
		key = L"FriendlyName" + std::to_wstring(i);
		std::wstring tunerFriendlyName = IniFileAccess.ReadKeySSectionData(key, L"");
		key = L"CaptureGUID" + std::to_wstring(i);
		std::wstring captureGuid = IniFileAccess.ReadKeySSectionData(key, L"");
		key = L"CaptureFriendlyName" + std::to_wstring(i);
		std::wstring captureFriendlyName = IniFileAccess.ReadKeySSectionData(key, L"");
		if (tunerGuid.length() == 0 && tunerFriendlyName.length() == 0 && captureGuid.length() == 0 && captureFriendlyName.length() == 0) {
			// ﾇＸ閧ｳﾄ?
			if (i == 0) {
				// ﾔ妻の型ﾅ?
				tunerGuid = IniFileAccess.ReadKeySSectionData(L"GUID", L"");
				tunerFriendlyName = IniFileAccess.ReadKeySSectionData(L"FriendlyName", L"");
				captureGuid = IniFileAccess.ReadKeySSectionData(L"CaptureGUID", L"");
				captureFriendlyName = IniFileAccess.ReadKeySSectionData(L"CaptureFriendlyName", L"");
				// ﾇＸ閧ｳﾄなれ合ﾅＰ^
			}
			else
				break;
		}
		m_aTunerParam.Tuner.emplace(i, TunerSearchData(tunerGuid, tunerFriendlyName, captureGuid, captureFriendlyName));
	}

	// TunerfoCXﾌ?CapturefoCXﾝ?
	m_aTunerParam.bNotExistCaptureDevice = IniFileAccess.ReadKeyBSectionData(L"NotExistCaptureDevice", FALSE);

	// TunerCaptureﾌデoCXCX^XpXvﾄるかﾌ確Fs
	m_aTunerParam.bCheckDeviceInstancePath = IniFileAccess.ReadKeyBSectionData(L"CheckDeviceInstancePath", TRUE);

	// Tuner: GetTunerNameﾅ返Ａ[i ... w閧ｳﾈばデtHg
	//   gBﾌ場合A`[i�ｼ前ﾅ玉る事ﾍで?
	m_aTunerParam.sTunerName = common::WStringToTString(IniFileAccess.ReadKeySSectionData(L"Name", L"DVB-S2"));

	// `[iﾅ有ﾖ身p驍ｩB
	//    INI t@Cﾅ指ﾂ能
	//     "" ... gp(default); "AUTO" ... AUTO
	//     "DLLName" ... `[iﾅ有ﾖ昔泥LL�ｼ接指
	m_aTunerParam.sDLLBaseName = IniFileAccess.ReadKeySSectionData(L"UseSpecial", L"");

	// ToneMﾘ替燦ait
	m_nToneWait = IniFileAccess.ReadKeyISectionData(L"ToneSignalWait", 100);

	// CHﾘ替鍬ockmF
	m_nLockWait = IniFileAccess.ReadKeyISectionData(L"ChannelLockWait", 2000);

	// CHﾘ替鍬ockmFDelay
	m_nLockWaitDelay = IniFileAccess.ReadKeyISectionData(L"ChannelLockWaitDelay", 0);

	// CHﾘ替鍬ockmFRetry
	m_nLockWaitRetry = IniFileAccess.ReadKeyISectionData(L"ChannelLockWaitRetry", 0);

	// CHﾘ替的2xs
	m_bLockTwice = IniFileAccess.ReadKeyBSectionData(L"ChannelLockTwice", FALSE);

	// CHﾘ替的2xs鼾⑤elay
	m_nLockTwiceDelay = IniFileAccess.ReadKeyISectionData(L"ChannelLockTwiceDelay", 100);

	// SignalLockﾌ異常検m(b)
	m_nWatchDogSignalLocked = IniFileAccess.ReadKeyISectionData(L"WatchDogSignalLocked", 0);

	// BitRateﾌ異常検m(b)
	m_nWatchDogBitRate = IniFileAccess.ReadKeyISectionData(L"WatchDogBitRate", 0);

	// ﾙ常検mA`[iﾌ再オ[vﾝま?CHﾘ替南試s
	m_nReOpenWhenGiveUpReLock = IniFileAccess.ReadKeyISectionData(L"ReOpenWhenGiveUpReLock", 0);

	// `[iﾌ再オ[vﾝれ合ﾉ別のチ[iD謔ｵﾄ碁か
	m_bTryAnotherTuner = IniFileAccess.ReadKeyBSectionData(L"TryAnotherTuner", FALSE);

	// CHﾘ替に市ﾉ、ﾙ常検mlobNOhCHﾘ替都
	m_bBackgroundChannelLock = IniFileAccess.ReadKeyBSectionData(L"BackgroundChannelLock", FALSE);

	// Tuning Spaceiﾝ角j
	std::wstring sTempTuningSpaceName = IniFileAccess.ReadKeySSectionData(L"TuningSpaceName", L"XJp[");

	// SignalLevel Zo@
	m_nSignalLevelCalcType = (enumSignalLevelCalcType)IniFileAccess.ReadKeyIValueMapSectionData(L"SignalLevelCalcType", enumSignalLevelCalcType::eSignalLevelCalcTypeSSStrength, mapSignalLevelCalcType);
	if (m_nSignalLevelCalcType >= eSignalLevelCalcTypeSSMin && m_nSignalLevelCalcType <= eSignalLevelCalcTypeSSMax)
		m_bSignalLevelGetTypeSS = TRUE;
	else if (m_nSignalLevelCalcType >= eSignalLevelCalcTypeTunerMin && m_nSignalLevelCalcType <= eSignalLevelCalcTypeTunerMax)
		m_bSignalLevelGetTypeTuner = TRUE;
	else if (m_nSignalLevelCalcType >= eSignalLevelCalcTypeDemodSSMin && m_nSignalLevelCalcType <= eSignalLevelCalcTypeDemodSSMax)
		m_bSignalLevelGetTypeDemodSS = TRUE;
	else if (m_nSignalLevelCalcType == eSignalLevelCalcTypeBR)
		m_bSignalLevelGetTypeBR = TRUE;

	if (m_nSignalLevelCalcType == eSignalLevelCalcTypeSSStrength || m_nSignalLevelCalcType == eSignalLevelCalcTypeTunerStrength || m_nSignalLevelCalcType == eSignalLevelCalcTypeDemodSSStrength) {
		m_bSignalLevelNeedStrength = TRUE;
		m_sSignalLevelCalcFormula = L"S / SC + SB";
	}
	if (m_nSignalLevelCalcType == eSignalLevelCalcTypeSSQuality || m_nSignalLevelCalcType == eSignalLevelCalcTypeTunerQuality || m_nSignalLevelCalcType == eSignalLevelCalcTypeDemodSSQuality) {
		m_bSignalLevelNeedQuality = TRUE;
		m_sSignalLevelCalcFormula = L"Q / QC + QB";
	}
	if (m_nSignalLevelCalcType == eSignalLevelCalcTypeSSMul || m_nSignalLevelCalcType == eSignalLevelCalcTypeTunerMul || m_nSignalLevelCalcType == eSignalLevelCalcTypeDemodSSMul) {
		m_bSignalLevelNeedStrength = TRUE;
		m_bSignalLevelNeedQuality = TRUE;
		m_sSignalLevelCalcFormula = L"(S / SC + SB) * (Q / QC + QB)";
	}
	if (m_nSignalLevelCalcType == eSignalLevelCalcTypeSSAdd || m_nSignalLevelCalcType == eSignalLevelCalcTypeTunerAdd || m_nSignalLevelCalcType == eSignalLevelCalcTypeDemodSSAdd) {
		m_bSignalLevelNeedStrength = TRUE;
		m_bSignalLevelNeedQuality = TRUE;
		m_sSignalLevelCalcFormula = L"(S / SC + SB) + (Q / QC + QB)";
	}
	if (m_nSignalLevelCalcType == eSignalLevelCalcTypeSSFormula || m_nSignalLevelCalcType == eSignalLevelCalcTypeTunerFormula || m_nSignalLevelCalcType == eSignalLevelCalcTypeDemodSSFormula) {
		m_bSignalLevelNeedStrength = TRUE;
		m_bSignalLevelNeedQuality = TRUE;
		m_sSignalLevelCalcFormula = IniFileAccess.ReadKeySSectionData(L"SignalLevelCalcFormula", L"S / SC + SB");
	}

	// Strength l竦ｳW
	m_fStrengthCoefficient = (double)IniFileAccess.ReadKeyFSectionData(L"StrengthCoefficient", 1.0);
	if (m_fStrengthCoefficient == 0.0)
		m_fStrengthCoefficient = 1.0;

	// Quality l竦ｳW
	m_fQualityCoefficient = (double)IniFileAccess.ReadKeyFSectionData(L"QualityCoefficient", 1.0);
	if (m_fQualityCoefficient == 0.0)
		m_fQualityCoefficient = 1.0;

	// Strength l竦ｳoCAX
	m_fStrengthBias = (double)IniFileAccess.ReadKeyFSectionData(L"StrengthBias", 0.0);

	// Quality l竦ｳoCAX
	m_fQualityBias = (double)IniFileAccess.ReadKeyFSectionData(L"QualityBias", 0.0);

	// muparser
	try {
		m_muParser.DefineVar(_T("S"), &m_fStrength);
		m_muParser.DefineVar(_T("SC"), &m_fStrengthCoefficient);
		m_muParser.DefineVar(_T("SB"), &m_fStrengthBias);
		m_muParser.DefineVar(_T("Q"), &m_fQuality);
		m_muParser.DefineVar(_T("QC"), &m_fQualityCoefficient);
		m_muParser.DefineVar(_T("QB"), &m_fQualityBias);
		m_muParser.SetExpr(common::WStringToTString(m_sSignalLevelCalcFormula));
	}
	catch (...) {
		OutputDebug(L"muParser exception. Wrong formula format?\n");
	}

	// `[jOﾔの杷@
	m_nSignalLockedJudgeType = (enumSignalLockedJudgeType)IniFileAccess.ReadKeyIValueMapSectionData(L"SignalLockedJudgeType", enumSignalLockedJudgeType::eSignalLockedJudgeTypeSS, mapSignalLockedJudgeType);
	if (m_nSignalLockedJudgeType == eSignalLockedJudgeTypeSS)
		m_bSignalLockedJudgeTypeSS = TRUE;
	else if (m_nSignalLockedJudgeType == eSignalLockedJudgeTypeTuner)
		m_bSignalLockedJudgeTypeTuner = TRUE;
	else if (m_nSignalLockedJudgeType == eSignalLockedJudgeTypeDemodSS)
		m_bSignalLockedJudgeTypeDemodSS = TRUE;

	for (unsigned int i = 0; i < MAX_DVB_SYSTEM_TYPE; i++) {
		std::wstring key, prefix[2];
		DVBSystemTypeData typeData;
		if (i == 0) {
			// `[i[ﾌ使pTuningSpace
			typeData.nDVBSystemType = enumTunerType::eTunerTypeDVBS;
		}

		unsigned int st = i == 0 ? 0 : 1;
		prefix[0] = L"DVBSystemType";
		prefix[1] = L"DVBSystemType" + std::to_wstring(i);
		for (unsigned int j = st; j < 2; j++) {
			// `[i[ﾌ使pTuningSpace
			key = prefix[j];
			typeData.nDVBSystemType = (enumTunerType)IniFileAccess.ReadKeyIValueMapSectionData(key, typeData.nDVBSystemType, mapTuningSpaceType);

			// gpITuningSpace interface
			key = prefix[j] + L"TuningSpace";
			typeData.nTuningSpace = (enumTuningSpace)IniFileAccess.ReadKeyIValueMapSectionData(key, typeData.nTuningSpace, mapSpecifyTuningSpace);

			// gpILocator interface
			key = prefix[j] + L"Locator";
			typeData.nLocator = (enumLocator)IniFileAccess.ReadKeyIValueMapSectionData(key, typeData.nLocator, mapSpecifyLocator);

			// ITuningSpaceﾉ設定すNetworkType
			key = prefix[j] + L"ITuningSpaceNetworkType";
			typeData.nITuningSpaceNetworkType = (enumNetworkType)IniFileAccess.ReadKeyIValueMapSectionData(key, typeData.nITuningSpaceNetworkType, mapSpecifyITuningSpaceNetworkType);

			// IDVBTuningSpaceﾉ設定すSystemType
			key = prefix[j] + L"IDVBTuningSpaceSystemType";
			typeData.nIDVBTuningSpaceSystemType = (enumDVBSystemType)IniFileAccess.ReadKeyIValueMapSectionData(key, typeData.nIDVBTuningSpaceSystemType, mapSpecifyIDVBTuningSpaceSystemType);

			// IAnalogTVTuningSpaceﾉ設定すInputType
			key = prefix[j] + L"IAnalogTVTuningSpaceInputType";
			typeData.nIAnalogTVTuningSpaceInputType = (enumTunerInputType)IniFileAccess.ReadKeyIValueMapSectionData(key, typeData.nIAnalogTVTuningSpaceInputType, mapSpecifyIAnalogTVTuningSpaceInputType);
		}

		if (typeData.nDVBSystemType == enumTunerType::eTunerTypeNone && (typeData.nTuningSpace == enumTuningSpace::eTuningSpaceAuto || typeData.nLocator == enumLocator::eLocatorAuto)) {
			continue;
		}

		// DBﾉ登^
		auto it = m_DVBSystemTypeDB.SystemType.find(i);
		if (it == m_DVBSystemTypeDB.SystemType.end()) {
			it = m_DVBSystemTypeDB.SystemType.emplace(i, typeData).first;
			m_DVBSystemTypeDB.nNumType++;
		}
	}

	// `[i[ﾉ使pNetworkProvider
	m_nNetworkProvider = (enumNetworkProvider)IniFileAccess.ReadKeyIValueMapSectionData(L"NetworkProvider", enumNetworkProvider::eNetworkProviderAuto, mapNetworkProvider);

	// qMp[^/ﾏ恥[^ﾌデtHgl
	m_nDefaultNetwork = (enumDefaultNetwork)IniFileAccess.ReadKeyIValueMapSectionData(L"DefaultNetwork", enumDefaultNetwork::eDefaultNetworkSPHD, mapDefaultNetwork);

	// tB^OtRunningObjectTableﾉ登^驍ｩ
	m_bRegisterGraphInROT = IniFileAccess.ReadKeyBSectionData(L"RegisterGraphInROT", FALSE);

	// dvﾏ更GUID
	m_sPowerSetOnOpenedGUID = IniFileAccess.ReadKeySSectionData(L"PowerSetOnOpenedGUID", L"");
	m_sPowerSetOnClosingGUID = IniFileAccess.ReadKeySSectionData(L"PowerSetOnClosingGUID", L"");

	//
	// BonDriver ZNV
	//
	IniFileAccess.ReadSection(L"BONDRIVER");
	IniFileAccess.CreateSectionData();

	// Xg[f[^obt@1ﾂ蔑サCY
	// 188~ﾝ定数(bytes)
	m_nBuffSize = 188 * (size_t)IniFileAccess.ReadKeyISectionData(L"BuffSize", 1024);

	// Xg[f[^obt@ﾌ最?
	m_nMaxBuffCount = (size_t)IniFileAccess.ReadKeyISectionData(L"MaxBuffCount", 512);

	// WaitTsStreamAw閧ｳ黷ｽﾂ昔スg[f[^obt@ﾜまで待機
	// `[iCPUﾗとは人蛯ｫﾚにと弧れ合
	m_nWaitTsCount = IniFileAccess.ReadKeyISectionData(L"WaitTsCount", 1);
	if (m_nWaitTsCount < 1)
		m_nWaitTsCount = 1;

	// WaitTsStreamXg[f[^obt@ﾜてなれ合ﾉ最辻機(msec)
	// `[iCPUﾗ?100msecxw閧ｷﾆ弧れ合
	m_nWaitTsSleep = IniFileAccess.ReadKeyISectionData(L"WaitTsSleep", 100);

	// kpPbg尞憺か
	m_bDeleteNullPackets = IniFileAccess.ReadKeyBSectionData(L"DeleteNullPackets", FALSE);

	// SetChannel()ﾅチlbNﾉ市FALSEﾔなようﾉるか
	m_bAlwaysAnswerLocked = IniFileAccess.ReadKeyBSectionData(L"AlwaysAnswerLocked", FALSE);

	// COMProcThreadﾌスbhvCIeB
	m_nThreadPriorityCOM = IniFileAccess.ReadKeyIValueMapSectionData(L"ThreadPriorityCOM", THREAD_PRIORITY_ERROR_RETURN, mapThreadPriority);

	// DecodeProcThreadﾌスbhvCIeB
	m_nThreadPriorityDecode = IniFileAccess.ReadKeyIValueMapSectionData(L"ThreadPriorityDecode", THREAD_PRIORITY_ERROR_RETURN, mapThreadPriority);

	// Xg[XbhvCIeB
	m_nThreadPriorityStream = IniFileAccess.ReadKeyIValueMapSectionData(L"ThreadPriorityStream", THREAD_PRIORITY_ERROR_RETURN, mapThreadPriority);

	// timeBeginPeriod()ﾅ設定すWindowsﾌ最戎C}\(msec)
	m_nPeriodicTimer = IniFileAccess.ReadKeyISectionData(L"PeriodicTimer", 0);

	//
	// Satellite ZNV
	//
	IniFileAccess.ReadSection(L"SATELLITE");
	IniFileAccess.CreateSectionData();

	// qﾊ皿p[^
	std::wstring sateliteSettingsAuto[MAX_SATELLITE];

	// Satellite0`ﾍ衛ﾝ定無piinit@Cﾌ読栽行ﾈＫ
	m_sSatelliteName[0] = L"not set";						// `lpq
	// ﾌ以外ﾍコXgN^ﾌデtHglgp

	// DefaultNetworkﾉＧtHgl
	switch (m_nDefaultNetwork) {
	case eDefaultNetworkSPHD:
		// SPHD
		sateliteSettingsAuto[1] = L"JCSAT-3";
		sateliteSettingsAuto[2] = L"JCSAT-4";
		break;

	case eDefaultNetworkBSCS:
	case eDefaultNetworkDual:
		sateliteSettingsAuto[1] = L"BS/CS110";
		break;
	}

	// BS/CS110CHﾝ定自ﾉ使pqﾝ?
	unsigned int satelliteNumberBSCS110R = 1;

	// SPHDCHﾝ定自ﾉ使pqﾝ?
	unsigned int satelliteNumberJCSAT3 = 1;
	unsigned int satelliteNumberJCSAT4 = 2;

	// q1`9ﾌ設?
	for (unsigned int satellite = 1; satellite < MAX_SATELLITE; satellite++) {
		std::wstring key, prefix1, prefix2;
		prefix1 = L"Satellite" + std::to_wstring(satellite);

		// qﾝ定自
		key = prefix1 + L"SettingsAuto";
		sateliteSettingsAuto[satellite] = common::WStringToUpperCase(IniFileAccess.ReadKeySSectionData(key, sateliteSettingsAuto[satellite]));

		if (sateliteSettingsAuto[satellite] == L"JCSAT-3") {
			// JCSAT-3A
			satelliteNumberJCSAT3 = satellite;
			m_sSatelliteName[satellite] = L"128.0E";																					// `lpq
			m_aSatellite[satellite].Polarisation[1].HighOscillator = m_aSatellite[satellite].Polarisation[1].LowOscillator = 11200000;	// ﾎ波LNBg
			m_aSatellite[satellite].Polarisation[1].Tone = 0;																			// ﾎ波g[M
			m_aSatellite[satellite].Polarisation[2].HighOscillator = m_aSatellite[satellite].Polarisation[2].LowOscillator = 11200000;	// ﾎ波LNBg
			m_aSatellite[satellite].Polarisation[2].Tone = 0;																			// ﾎ波g[M
		}
		else if (sateliteSettingsAuto[satellite] == L"JCSAT-4") {
			// JCSAT-4B
			satelliteNumberJCSAT4 = satellite;
			m_sSatelliteName[satellite] = L"124.0E";																					// `lpq
			m_aSatellite[satellite].Polarisation[1].HighOscillator = m_aSatellite[satellite].Polarisation[1].LowOscillator = 11200000;	// ﾎ波LNBg
			m_aSatellite[satellite].Polarisation[1].Tone = 1;																			// ﾎ波g[M
			m_aSatellite[satellite].Polarisation[2].HighOscillator = m_aSatellite[satellite].Polarisation[2].LowOscillator = 11200000;	// ﾎ波LNBg
			m_aSatellite[satellite].Polarisation[2].Tone = 1;																			// ﾎ波g[M
		}
		else if (sateliteSettingsAuto[satellite] == L"BS/CS110") {
			// BS/CS110 E~ﾎ波ﾆ降ﾎ波
			satelliteNumberBSCS110R = satellite;
			m_sSatelliteName[satellite] = L"BS/CS110";																					// `lpq
			m_aSatellite[satellite].Polarisation[3].HighOscillator = m_aSatellite[satellite].Polarisation[3].LowOscillator = 9505000;	// ~ﾎ波LNBg
			m_aSatellite[satellite].Polarisation[3].Tone = 0;																			// ~ﾎ波ﾎ波g[M
			m_aSatellite[satellite].Polarisation[4].HighOscillator = m_aSatellite[satellite].Polarisation[4].LowOscillator = 10678000;	// E~ﾎ波LNBg
			m_aSatellite[satellite].Polarisation[4].Tone = 0;																			// E~ﾎ波ﾎ波g[M
		}

		// T[rX\pq
		key = prefix1 + L"Name";
		m_sSatelliteName[satellite] = IniFileAccess.ReadKeySSectionData(key, m_sSatelliteName[satellite]);

		// ﾎ波1`4ﾌアeiﾝ?
		for (unsigned int polarisation = 1; polarisation < POLARISATION_SIZE; polarisation++) {
			prefix2 = prefix1 + PolarisationChar[polarisation];
			// ﾇ波 (KHz)
			// Sﾎ波ﾊでの設定がﾎ読?
			key = prefix1 + L"Oscillator";
			m_aSatellite[satellite].Polarisation[polarisation].LowOscillator = m_aSatellite[satellite].Polarisation[polarisation].HighOscillator
				= (long)IniFileAccess.ReadKeyISectionData(key, m_aSatellite[satellite].Polarisation[polarisation].HighOscillator);
			key = prefix1 + L"HighOscillator";
			m_aSatellite[satellite].Polarisation[polarisation].HighOscillator
				= (long)IniFileAccess.ReadKeyISectionData(key, m_aSatellite[satellite].Polarisation[polarisation].HighOscillator);
			key = prefix1 + L"LowOscillator";
			m_aSatellite[satellite].Polarisation[polarisation].LowOscillator
				= (long)IniFileAccess.ReadKeyISectionData(key, m_aSatellite[satellite].Polarisation[polarisation].LowOscillator);
			// ﾂ別設定がﾎ上書ﾅ読?
			key = prefix2 + L"Oscillator";
			m_aSatellite[satellite].Polarisation[polarisation].LowOscillator = m_aSatellite[satellite].Polarisation[polarisation].HighOscillator
				= (long)IniFileAccess.ReadKeyISectionData(key, m_aSatellite[satellite].Polarisation[polarisation].HighOscillator);
			key = prefix2 + L"HighOscillator";
			m_aSatellite[satellite].Polarisation[polarisation].HighOscillator
				= (long)IniFileAccess.ReadKeyISectionData(key, m_aSatellite[satellite].Polarisation[polarisation].HighOscillator);
			key = prefix2 + L"LowOscillator";
			m_aSatellite[satellite].Polarisation[polarisation].LowOscillator
				= (long)IniFileAccess.ReadKeyISectionData(key, m_aSatellite[satellite].Polarisation[polarisation].LowOscillator);

			// LNBﾘ替使 (KHz)
			// Sﾎ波ﾊでの設定がﾎ読?
			key = prefix1 + L"LNBSwitch";
			m_aSatellite[satellite].Polarisation[polarisation].LNBSwitch
				= (long)IniFileAccess.ReadKeyISectionData(key, m_aSatellite[satellite].Polarisation[polarisation].LNBSwitch);
			// ﾂ別設定がﾎ上書ﾅ読?
			key = prefix2 + L"LNBSwitch";
			m_aSatellite[satellite].Polarisation[polarisation].LNBSwitch
				= (long)IniFileAccess.ReadKeyISectionData(key, m_aSatellite[satellite].Polarisation[polarisation].LNBSwitch);

			// g[M (0 or 1)
			// Sﾎ波ﾊでの設定がﾎ読?
			key = prefix1 + L"ToneSignal";
			m_aSatellite[satellite].Polarisation[polarisation].Tone
				= (long)IniFileAccess.ReadKeyBSectionData(key, m_aSatellite[satellite].Polarisation[polarisation].Tone);
			// ﾂ別設定がﾎ上書ﾅ読?
			key = prefix2 + L"ToneSignal";
			m_aSatellite[satellite].Polarisation[polarisation].Tone
				= (long)IniFileAccess.ReadKeyBSectionData(key, m_aSatellite[satellite].Polarisation[polarisation].Tone);

			// DiSEqC
			// Sﾎ波ﾊでの設定がﾎ読?
			key = prefix1 + L"DiSEqC";
			m_aSatellite[satellite].Polarisation[polarisation].DiSEqC
				= (long)IniFileAccess.ReadKeyIValueMapSectionData(key, m_aSatellite[satellite].Polarisation[polarisation].DiSEqC, mapDiSEqC);
			// ﾂ別設定がﾎ上書ﾅ読?
			key = prefix2 + L"DiSEqC";
			m_aSatellite[satellite].Polarisation[polarisation].DiSEqC
				= (long)IniFileAccess.ReadKeyIValueMapSectionData(key, m_aSatellite[satellite].Polarisation[polarisation].DiSEqC, mapDiSEqC);
		}
	}

	//
	// Modulation ZNV
	//
	IniFileAccess.ReadSection(L"MODULATION");
	IniFileAccess.CreateSectionData();

	// ﾏ通パ[^i0`9ﾌ省ので蔦）
	std::wstring modulationSettingsAuto[MAX_MODULATION];

	// DefaultNetworkﾉＧtHgl
	switch (m_nDefaultNetwork) {
	case eDefaultNetworkSPHD:
		//SPHD
		modulationSettingsAuto[0] = L"DVB-S";
		modulationSettingsAuto[1] = L"DVB-S2";
		break;

	case eDefaultNetworkBSCS:
		// BS/CS110
		modulationSettingsAuto[0] = L"ISDB-S";
		break;

	case eDefaultNetworkUHF:
		// UHF/CATV
		modulationSettingsAuto[0] = L"ISDB-T";
		break;

	case eDefaultNetworkDual:
		modulationSettingsAuto[0] = L"ISDB-T";
		modulationSettingsAuto[1] = L"ISDB-S";
		break;
	}

	// UHF / CATVCHﾝ定自ﾉ使pﾏ?
	unsigned int modulationNumberISDBT = 0;

	// BS/CS110CHﾝ定自ﾉ使pﾏ?
	unsigned int modulationNumberISDBS = 0;

	// SPHDCHﾝ定自ﾉ使pﾏ?
	unsigned int modulationNumberDVBS = 0;
	unsigned int modulationNumberDVBS2 = 1;

	// ISDB-CgXW[VCHﾝ定自ﾉ使pﾏ?
	unsigned int modulationNumberJ83C64QAM = 0;
	unsigned int modulationNumberJ83C256QAM = 1;

	// XJp[!v~AT[rXCHﾝ定自ﾉ使pﾏ?
	unsigned int modulationNumberOpticast = 0;

	// ﾏ?0`9ﾌ値
	for (unsigned int modulation = 0; modulation < MAX_MODULATION; modulation++) {
		std::wstring key, prefix;
		prefix = L"ModulationType" + std::to_wstring(modulation);

		// ﾏ吊定自
		key = prefix + L"SettingsAuto";
		modulationSettingsAuto[modulation] = common::WStringToUpperCase(IniFileAccess.ReadKeySSectionData(key, modulationSettingsAuto[modulation]));

		if (modulationSettingsAuto[modulation] == L"DVB-S") {
			// SPHD DVB-S
			modulationNumberDVBS = modulation;
			m_sModulationName[modulation] = L"DVB-S";							// `lp
			m_aModulationType[modulation].Modulation = BDA_MOD_QPSK;			// ﾏ耽Cv
			m_aModulationType[modulation].InnerFEC = BDA_FEC_VITERBI;			// O^Cv
			m_aModulationType[modulation].InnerFECRate = BDA_BCC_RATE_3_4;		// FEC[g
			m_aModulationType[modulation].OuterFEC = BDA_FEC_RS_204_188;		// OO^Cv
			m_aModulationType[modulation].OuterFECRate = BDA_BCC_RATE_NOT_SET;	// OFEC[g
			m_aModulationType[modulation].SymbolRate = 21096;					// V{[g
		}
		else if (modulationSettingsAuto[modulation] == L"DVB-S2") {
			// SPHD DVB-S2
			modulationNumberDVBS2 = modulation;
			m_sModulationName[modulation] = L"DVB-S2";							// `lp
			m_aModulationType[modulation].Modulation = BDA_MOD_NBC_8PSK;		// ﾏ耽Cv
			m_aModulationType[modulation].InnerFEC = BDA_FEC_LDPC;				// O^Cv
			m_aModulationType[modulation].InnerFECRate = BDA_BCC_RATE_3_5;		// FEC[g
			m_aModulationType[modulation].OuterFEC = BDA_FEC_BCH;				// OO^Cv
			m_aModulationType[modulation].OuterFECRate = BDA_BCC_RATE_NOT_SET;	// OFEC[g
			m_aModulationType[modulation].SymbolRate = 23303;					// V{[g
		}
		else if (modulationSettingsAuto[modulation] == L"ISDB-S") {
			// BS/CS110
			modulationNumberISDBS = modulation;
			m_sModulationName[modulation] = L"ISDB-S";							// `lp
			m_aModulationType[modulation].Modulation = BDA_MOD_ISDB_S_TMCC;		// ﾏ耽Cv
			m_aModulationType[modulation].InnerFEC = BDA_FEC_VITERBI;			// O^Cv
			m_aModulationType[modulation].InnerFECRate = BDA_BCC_RATE_2_3;		// FEC[g
			m_aModulationType[modulation].OuterFEC = BDA_FEC_RS_204_188;		// OO^Cv
			m_aModulationType[modulation].OuterFECRate = BDA_BCC_RATE_NOT_SET;	// OFEC[g
			m_aModulationType[modulation].SymbolRate = 28860;					// V{[g
		}
		else if (modulationSettingsAuto[modulation] == L"ISDB-T") {
			// UHF/CATV
			modulationNumberISDBT = modulation;
			m_sModulationName[modulation] = L"ISDB-T";							// `lp
			m_aModulationType[modulation].Modulation = BDA_MOD_ISDB_T_TMCC;		// ﾏ耽Cv
			m_aModulationType[modulation].InnerFEC = BDA_FEC_VITERBI;			// O^Cv
			m_aModulationType[modulation].InnerFECRate = BDA_BCC_RATE_3_4;		// FEC[g
			m_aModulationType[modulation].OuterFEC = BDA_FEC_RS_204_188;		// OO^Cv
			m_aModulationType[modulation].OuterFECRate = BDA_BCC_RATE_NOT_SET;	// OFEC[g
			m_aModulationType[modulation].SymbolRate = -1;						// V{[g
			m_aModulationType[modulation].BandWidth = 6;						// ﾑ域幅(MHz)
		}
		else if (modulationSettingsAuto[modulation] == L"J.83C-64QAM") {
			// ISDB-CgXW[V 64QAM
			modulationNumberJ83C64QAM = modulation;
			m_sModulationName[modulation] = L"J.83C-64QAM";						// `lp
			m_aModulationType[modulation].Modulation = BDA_MOD_64QAM;			// ﾏ耽Cv
			m_aModulationType[modulation].InnerFEC = BDA_FEC_METHOD_NOT_SET;	// O^Cv
			m_aModulationType[modulation].InnerFECRate = BDA_BCC_RATE_NOT_SET;	// FEC[g
			m_aModulationType[modulation].OuterFEC = BDA_FEC_RS_204_188;		// OO^Cv
			m_aModulationType[modulation].OuterFECRate = BDA_BCC_RATE_NOT_SET;	// OFEC[g
			m_aModulationType[modulation].SymbolRate = 5274;					// V{[g
		}
		else if (modulationSettingsAuto[modulation] == L"J.83C-256QAM") {
			// ISDB-CgXW[V 256QAM
			modulationNumberJ83C256QAM = modulation;
			m_sModulationName[modulation] = L"J.83C-256QAM";					// `lp
			m_aModulationType[modulation].Modulation = BDA_MOD_256QAM;			// ﾏ耽Cv
			m_aModulationType[modulation].InnerFEC = BDA_FEC_METHOD_NOT_SET;	// O^Cv
			m_aModulationType[modulation].InnerFECRate = BDA_BCC_RATE_NOT_SET;	// FEC[g
			m_aModulationType[modulation].OuterFEC = BDA_FEC_RS_204_188;		// OO^Cv
			m_aModulationType[modulation].OuterFECRate = BDA_BCC_RATE_NOT_SET;	// OFEC[g
			m_aModulationType[modulation].SymbolRate = 5274;					// V{[g
		}
		else if (modulationSettingsAuto[modulation] == L"J.83B-OPTICAST") {
			// XJp[!v~AT[rX 256QAM
			modulationNumberOpticast = modulation;
			m_sModulationName[modulation] = L"J.83B-Opticast";					// `lp
			m_aModulationType[modulation].Modulation = BDA_MOD_256QAM;			// ﾏ耽Cv
			m_aModulationType[modulation].SymbolRate = 5600;					// V{[g
		}
		else if (modulationSettingsAuto[modulation] == L"J.83B-64QAM") {
			// J.83 Annex-B 64QAM
			m_sModulationName[modulation] = L"J.83B-64QAM";						// `lp
			m_aModulationType[modulation].Modulation = BDA_MOD_64QAM;			// ﾏ耽Cv
			m_aModulationType[modulation].SymbolRate = 5057;					// V{[g
		}
		else if (modulationSettingsAuto[modulation] == L"J.83B-256QAM") {
			// J.83 Annex-B 256QAM
			m_sModulationName[modulation] = L"J.83B-256QAM";					// `lp
			m_aModulationType[modulation].Modulation = BDA_MOD_256QAM;			// ﾏ耽Cv
			m_aModulationType[modulation].SymbolRate = 5361;					// V{[g
		}

		// `lp
		key = prefix + L"Name";
		m_sModulationName[modulation] = IniFileAccess.ReadKeySSectionData(key, m_sModulationName[modulation]);

		// ﾏ耽Cv
		key = prefix + L"Modulation";
		m_aModulationType[modulation].Modulation
			= (ModulationType)IniFileAccess.ReadKeyIValueMapSectionData(key, m_aModulationType[modulation].Modulation, mapModulationType);

		// O^Cv
		key = prefix + L"InnerFEC";
		m_aModulationType[modulation].InnerFEC
			= (FECMethod)IniFileAccess.ReadKeyIValueMapSectionData(key, m_aModulationType[modulation].InnerFEC, mapFECMethod);

		// FEC[g
		key = prefix + L"InnerFECRate";
		m_aModulationType[modulation].InnerFECRate
			= (BinaryConvolutionCodeRate)IniFileAccess.ReadKeyIValueMapSectionData(key, m_aModulationType[modulation].InnerFECRate, mapBinaryConvolutionCodeRate);

		// OO^Cv
		key = prefix + L"OuterFEC";
		m_aModulationType[modulation].OuterFEC
			= (FECMethod)IniFileAccess.ReadKeyIValueMapSectionData(key, m_aModulationType[modulation].OuterFEC, mapFECMethod);

		// OFEC[g
		key = prefix + L"OuterFECRate";
		m_aModulationType[modulation].OuterFECRate
			= (BinaryConvolutionCodeRate)IniFileAccess.ReadKeyIValueMapSectionData(key, m_aModulationType[modulation].OuterFECRate, mapBinaryConvolutionCodeRate);

		// V{[g
		key = prefix + L"SymbolRate";
		m_aModulationType[modulation].SymbolRate
			= (long)IniFileAccess.ReadKeyISectionData(key, m_aModulationType[modulation].SymbolRate);

		// ﾑ域幅(MHz)
		key = prefix + L"BandWidth";
		m_aModulationType[modulation].BandWidth
			= (long)IniFileAccess.ReadKeyISectionData(key, m_aModulationType[modulation].BandWidth);
	}

	//
	// Channel ZNV
	//

	// init@CCHﾝ追材際に使pﾄ?CHﾔ再０lmﾛて?
	// [Channel]ZNVﾅの蛋 ... Sﾄのチ[jOﾔに影
	BOOL bReserveUnusedChGlobal = IniFileAccess.ReadKeyB(L"CHANNEL", L"ReserveUnusedCh", FALSE);

	// `[jO00`99ﾌ設?
	for (DWORD space = 0; space < 100; space++)	{
		std::wstring section = common::WStringPrintf(L"TUNINGSPACE%02d", space);
		if (IniFileAccess.ReadSection(section) <= 0) {
			// TuningSpaceXXﾌセNVﾝなれ合
			if (space != 0)
				continue;
			// TuningSpace00ﾌ擦hannelZNV
			IniFileAccess.ReadSection(L"CHANNEL");
		}
		IniFileAccess.CreateSectionData();

		// init@CCHﾝ追材際に使pﾄ?CHﾔ再０lmﾛて?
		// [TuningSpaceXX]ZNVﾅの蛋 ... ﾝのチ[jOﾔに影
		BOOL bReserveUnusedCh = IniFileAccess.ReadKeyBSectionData(L"ReserveUnusedCh", bReserveUnusedChGlobal);

		// ﾉチ[jOﾔデ[^ﾝれ合ﾍの兎
		// ﾍ粁チ[jOﾔゎ成
		auto itSpace = m_TuningData.Spaces.find(space);
		if (itSpace == m_TuningData.Spaces.end()) {
			itSpace = m_TuningData.Spaces.emplace(space, TuningSpaceData()).first;
		}

		// Tuning Space
		std::wstring temp;
		if (space == 0)
			temp = sTempTuningSpaceName;
		else
			temp = L"NoName";
		
		itSpace->second.sTuningSpaceName = common::WStringToTString(IniFileAccess.ReadKeySSectionData(L"TuningSpaceName", temp));

		// init@C1ﾂのチ[jOﾔで蛋ﾅで舛hannelGenerate
		static constexpr unsigned int MAX_CH_GENERATE = 100;

		// `l^Cv
		enum enumChGenerate {
			eChGenerateNone = 0,
			eChGenerateVHF_L = 1,				// VHF 1ch`3ch
			eChGenerateVHF_H = 2,				// VHF 4ch`12ch
			eChGenerateUHF = 3,					// UHF 13ch`62ch
			eChGenerateCATV_L = 4,				// CATV C13ch`C22ch
			eChGenerateCATV_H = 5,				// CATV C23ch`C63ch
			eChGenerateVHF_4Plus = 8,			// VHF 4ch+
			eChGenerateCATV_22Plus = 9,			// CATV C22ch+
			eChGenerateCATV_24Plus = 10,		// CATV C24ch+`C27ch+
			eChGenerateBS1 = 16,				// BS-R BS1,BS3,BS5...`BS23
			eChGenerateND2 = 20,				// CS110-R ND2,ND4,ND6...`ND24
			eChGenerateJD1 = 32,				// JCSAT-3A/JCSAT-4B JD1`JD16
			eChGenerateJD17A = 33,				// JCSAT-3A JD17`JD28
			eChGenerateJD17B = 34,				// JCSAT-4B JD17`JD32
			eChGenerateOpticast = 64,			// SKY PerfecTV! Premium Service Hikari H001`H058
			eChGenerateOpticast_11Plus = 72,	// SKY PerfecTV! Premium Service Hikari H011+`H012+
			eChGenerateOpticast_26Plus = 73,	// SKY PerfecTV! Premium Service Hikari H026+`H028+
		};

		// `lpp[^
		struct ChGenerate {
			enumChGenerate Space;
			unsigned int Offset;			// JnItZbg
			unsigned int Count;				// CH
			unsigned int RelativeTS;		// TS
			unsigned int ModulationNumber;	// ﾏ?
			unsigned int SatelliteNumber;	// q
			unsigned int StartCh;			// 謫ｪ`l
			unsigned int TuningFreq;		// `[jOgItZbg
			std::wstring NameFormat;		// `ltH[}bg
			unsigned int NameOffset;		// `lﾉ使p`lﾔ巧tZbg
			unsigned int NameStep;			// `lﾉ使p`lﾔ更ebv
			unsigned int NameOffsetTS;		// `lﾉ使pTSﾔ巧tZbg
			ChGenerate(void)
				: Space(eChGenerateNone),
				Offset(0),
				Count(0),
				RelativeTS(0),
				ModulationNumber(0),
				SatelliteNumber(0),
				StartCh(0),
				TuningFreq(0),
				NameOffset(0),
				NameStep(1),
				NameOffsetTS(0)
			{
			};
			~ChGenerate(void)
			{
			};
		};

		// `lﾌ餐s
		std::wstring ChannelGenerate[MAX_CH_GENERATE];

		// UHF/CATVCH
		std::wstring chAuto = common::WStringToUpperCase(IniFileAccess.ReadKeySSectionData(L"ChannelSettingsAuto", L""));
		std::wstring chAutoOpt = common::WStringToUpperCase(IniFileAccess.ReadKeySSectionData(L"ChannelSettingsAutoOptions", L""));
		BOOL bOptVHFPlus = FALSE;
		BOOL bOptC24Plus = FALSE;
		BOOL bOptOnly64QAM = FALSE;
		BOOL bOptOnly256QAM = FALSE;
		BOOL bOptH11Plus = FALSE;
		BOOL bOptH26Plus = FALSE;
		BOOL bOptOnlySD = FALSE;
		BOOL bOptOnlyHD = FALSE;
		BOOL bOptSpinel = FALSE;
		BOOL bOptAll = FALSE;
		std::wstring sOptRelativeTS = L"";
		std::wstring sOptRelativeTS64QAM = L"";
		{
			// J}7ﾂ?
			std::wstring t(chAutoOpt);
			for (int n = 0; n < 10; n++) {
				std::wstring opt;
				std::wstring::size_type pos = common::WStringSplit(&t, L',', &opt);
				if (opt == L"VHF+") {
					bOptVHFPlus = TRUE;
				}
				else if (opt == L"C24+") {
					bOptC24Plus = TRUE;
				}
				else if (opt == L"ONLY64QAM") {
					bOptOnly64QAM = TRUE;
				}
				else if (opt == L"ONLY256QAM") {
					bOptOnly256QAM = TRUE;
				}
				else if (opt == L"H011+") {
					bOptH11Plus = TRUE;
				}
				else if (opt == L"H026+") {
					bOptH26Plus = TRUE;
				}
				else if (opt == L"ONLYSD") {
					bOptOnlySD = TRUE;
				}
				else if (opt == L"ONLYHD") {
					bOptOnlyHD = TRUE;
				}
				else if (opt == L"SPINEL") {
					bOptSpinel = TRUE;
				}
				else if (opt == L"ALL") {
					bOptAll = TRUE;
				}
				else if (opt.substr(0, 11) == L"RELATIVETS:") {
					sOptRelativeTS = opt.substr(11);
				}
				else if (opt.substr(0, 18) == L"RELATIVETS-256QAM:") {
					sOptRelativeTS = opt.substr(18);
				}
				else if (opt.substr(0, 17) == L"RELATIVETS-64QAM:") {
					sOptRelativeTS64QAM = opt.substr(17);
				}
				if (pos == std::wstring::npos)
					break;
			}

		}
		if (chAuto == L"UHF") {
			if (bOptAll) {
				ChannelGenerate[0] = L"UHF," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			else {
				ChannelGenerate[0] = L"UHF:0:40," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
		}
		else if (chAuto == L"CATV") {
			int num = 0;
			if (bOptAll || !bOptVHFPlus) {
				ChannelGenerate[num++] = L"CATV-L," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			else {
				ChannelGenerate[num++] = L"CATV-L:0:9," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"CATV-22+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			if (bOptAll || !bOptC24Plus) {
				ChannelGenerate[num++] = L"CATV-H," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			else {
				ChannelGenerate[num++] = L"CATV-H:0:1," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"CATV-24+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"CATV-H:5," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			if (bOptAll) {
				ChannelGenerate[num++] = L"CATV-22+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"CATV-24+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
		}
		else if (chAuto == L"PASSTHROUGH") {
			int num = 0;
			ChannelGenerate[num++] = L"VHF-L," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			if (bOptAll || !bOptVHFPlus) {
				ChannelGenerate[num++] = L"VHF-H," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			else {
				ChannelGenerate[num++] = L"VHF-4+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"VHF-H:4," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			ChannelGenerate[num++] = L"UHF," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			if (bOptAll || !bOptVHFPlus) {
				ChannelGenerate[num++] = L"CATV-L," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			else {
				ChannelGenerate[num++] = L"CATV-L:0:9," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"CATV-22+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			if (bOptAll || !bOptC24Plus) {
				ChannelGenerate[num++] = L"CATV-H," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			else {
				ChannelGenerate[num++] = L"CATV-H:0:1," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"CATV-24+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"CATV-H:5," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
			if (bOptAll) {
				ChannelGenerate[num++] = L"VHF-4+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"CATV-22+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
				ChannelGenerate[num++] = L"CATV-24+," + std::to_wstring(modulationNumberISDBT) + L",,,143";
			}
		}
		else if (chAuto == L"TRANSMODULATION") {
			std::wstring sRelative64;
			std::wstring sRelative256;
			if (sOptRelativeTS64QAM != L"") {
				sRelative64 = L":" + sOptRelativeTS64QAM;
			}
			if (sOptRelativeTS != L"") {
				sRelative256 = L":" + sOptRelativeTS;
			}
			int num = 0;
			unsigned int startCh = 0;
			if (!bOptOnly256QAM) {
				ChannelGenerate[num++] = L"VHF-L::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",," + std::to_wstring(startCh) + L",0,,,,1";
				if (bOptAll || !bOptVHFPlus) {
					ChannelGenerate[num++] = L"VHF-H::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
				}
				else {
					ChannelGenerate[num++] = L"VHF-4+::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"VHF-H:4:" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
				}
				ChannelGenerate[num++] = L"UHF::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
				if (bOptAll || !bOptVHFPlus) {
					ChannelGenerate[num++] = L"CATV-L::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
				}
				else {
					ChannelGenerate[num++] = L"CATV-L:0:9" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-22+::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
				}
				if (bOptAll || !bOptC24Plus) {
					ChannelGenerate[num++] = L"CATV-H::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
				}
				else {
					ChannelGenerate[num++] = L"CATV-H:0:1" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-24+::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-H:5:" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
				}
				startCh += max(common::WStringToLong(sOptRelativeTS64QAM), 1) * (12 + 50 + 51);
				if (bOptAll) {
					ChannelGenerate[num++] = L"VHF-4+::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-22+::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-24+::" + sRelative64 + L"," + std::to_wstring(modulationNumberJ83C64QAM) + L",,,0,,,,1";
					startCh += max(common::WStringToLong(sOptRelativeTS64QAM), 1) * (3 + 1 + 4);
				}
				startCh = ((startCh + 99) / 100) * 100;
			}
			if (!bOptOnly64QAM) {
				ChannelGenerate[num++] = L"VHF-L::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",," + std::to_wstring(startCh) + L",0,,,,1";
				if (bOptAll || !bOptVHFPlus) {
					ChannelGenerate[num++] = L"VHF-H::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
				}
				else {
					ChannelGenerate[num++] = L"VHF-4+::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"VHF-H:4:" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
				}
				ChannelGenerate[num++] = L"UHF::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
				if (bOptAll || !bOptVHFPlus) {
					ChannelGenerate[num++] = L"CATV-L::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
				}
				else {
					ChannelGenerate[num++] = L"CATV-L:0:9" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-22+::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
				}
				if (bOptAll || !bOptC24Plus) {
					ChannelGenerate[num++] = L"CATV-H::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
				}
				else {
					ChannelGenerate[num++] = L"CATV-H:0:1" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-24+::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-H:5:" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
				}
				if (bOptAll) {
					ChannelGenerate[num++] = L"VHF-4+::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-22+::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
					ChannelGenerate[num++] = L"CATV-24+::" + sRelative256 + L"," + std::to_wstring(modulationNumberJ83C256QAM) + L",,,0,,,,1";
				}
			}
		}
		else if (chAuto == L"BS") {
			std::wstring sRelative;
			if (sOptRelativeTS != L"") {
				sRelative = L":" + sOptRelativeTS;
			}
			int num = 0;
			ChannelGenerate[num++] = L"BS1::" + sRelative + L"," + std::to_wstring(modulationNumberISDBS) + L"," + std::to_wstring(satelliteNumberBSCS110R) + L",,,BS%02d/TS%d,1,2,0";
		}
		else if (chAuto == L"CS110") {
			int num = 0;
			ChannelGenerate[num++] = L"ND2," + std::to_wstring(modulationNumberISDBS) + L"," + std::to_wstring(satelliteNumberBSCS110R) + L",,,ND%02d/TS0,2,2";
		}
		else if (chAuto == L"SPHD") {
		std::wstring sName4 = bOptSpinel ? L"JCSAT4A-TP%02d" : L"JCSAT4B-TP%02d";
		int num = 0;
			unsigned int startCh = 1;
			if (!bOptOnlyHD) {
				ChannelGenerate[num++] = L"JD17A," + std::to_wstring(modulationNumberDVBS) + L"," + std::to_wstring(satelliteNumberJCSAT3) + L"," + std::to_wstring(startCh) + L",,JCSAT3A-TP%02d,1";
				ChannelGenerate[num++] = L"JD1," + std::to_wstring(modulationNumberDVBS) + L"," + std::to_wstring(satelliteNumberJCSAT3) + L",,,JCSAT3A-TP%02d,13";
				startCh += 100;
				ChannelGenerate[num++] = L"JD17B," + std::to_wstring(modulationNumberDVBS) + L"," + std::to_wstring(satelliteNumberJCSAT4) + L"," + std::to_wstring(startCh) + L",," + sName4 + L",1";
				ChannelGenerate[num++] = L"JD1," + std::to_wstring(modulationNumberDVBS) + L"," + std::to_wstring(satelliteNumberJCSAT4) + L",,," + sName4 + L",17";
				startCh += 100;
			}
			if (!bOptOnlySD) {
				ChannelGenerate[num++] = L"JD17A," + std::to_wstring(modulationNumberDVBS2) + L"," + std::to_wstring(satelliteNumberJCSAT3) + L"," + std::to_wstring(startCh) + L",,JCSAT3A-TP%02d,1";
				ChannelGenerate[num++] = L"JD1," + std::to_wstring(modulationNumberDVBS2) + L"," + std::to_wstring(satelliteNumberJCSAT3) + L",,,JCSAT3A-TP%02d,13";
				startCh += 100;
				ChannelGenerate[num++] = L"JD17B," + std::to_wstring(modulationNumberDVBS2) + L"," + std::to_wstring(satelliteNumberJCSAT4) + L"," + std::to_wstring(startCh) + L",," + sName4 + L",1";
				ChannelGenerate[num++] = L"JD1," + std::to_wstring(modulationNumberDVBS2) + L"," + std::to_wstring(satelliteNumberJCSAT4) + L",,," + sName4 + L",17";
			}
		}
		else if (chAuto == L"OPTICAST") {
			int num = 0;
			if (bOptAll || (!bOptH11Plus && !bOptH26Plus)) {
				ChannelGenerate[num++] = L"OPTICAST," + std::to_wstring(modulationNumberOpticast) + L",,,0";
			}
			else if (bOptH11Plus && !bOptH26Plus) {
				ChannelGenerate[num++] = L"OPTICAST:0:10," + std::to_wstring(modulationNumberOpticast) + L",,,0";
				ChannelGenerate[num++] = L"OPTICAST-11+," + std::to_wstring(modulationNumberOpticast) + L",,,0";
				ChannelGenerate[num++] = L"OPTICAST:12," + std::to_wstring(modulationNumberOpticast) + L",,,0";
			}
			else if (!bOptH11Plus && bOptH26Plus) {
				ChannelGenerate[num++] = L"OPTICAST:0:25," + std::to_wstring(modulationNumberOpticast) + L",,,0";
				ChannelGenerate[num++] = L"OPTICAST-26+," + std::to_wstring(modulationNumberOpticast) + L",,,0";
				ChannelGenerate[num++] = L"OPTICAST:28," + std::to_wstring(modulationNumberOpticast) + L",,,0";
			} else {
				ChannelGenerate[num++] = L"OPTICAST:0:10," + std::to_wstring(modulationNumberOpticast) + L",,,0";
				ChannelGenerate[num++] = L"OPTICAST-11+," + std::to_wstring(modulationNumberOpticast) + L",,,0";
				ChannelGenerate[num++] = L"OPTICAST:12:13," + std::to_wstring(modulationNumberOpticast) + L",,,0";
				ChannelGenerate[num++] = L"OPTICAST-26+," + std::to_wstring(modulationNumberOpticast) + L",,,0";
				ChannelGenerate[num++] = L"OPTICAST:28," + std::to_wstring(modulationNumberOpticast) + L",,,0";
			}
			if (bOptAll) {
				ChannelGenerate[num++] = L"OPTICAST-11+," + std::to_wstring(modulationNumberOpticast) + L",,,0";
				ChannelGenerate[num++] = L"OPTICAST-26+," + std::to_wstring(modulationNumberOpticast) + L",,,0";
			}
		}

		unsigned int nextCh = 0;	// 謫ｪ`l

		// init@Cﾌ?
		for (int i = 0; i < MAX_CH_GENERATE; i++) {
			ChGenerate Generate;
			std::wstring key = L"ChannelGenerate" + std::to_wstring(i);
			ChannelGenerate[i] = IniFileAccess.ReadKeySSectionData(key, ChannelGenerate[i]);
			if (ChannelGenerate[i].length() == 0)
				break;
			// J}9ﾂ?
			std::wstring t(ChannelGenerate[i]);
			std::wstring buf[9];
			for (int n = 0; n < 9; n++) {
				if (std::wstring::npos == common::WStringSplit(&t, L',', &buf[n]))
					break;
			}
			// :JnItZbg:CH:TS 
			std::wstring t2(buf[0]);
			std::wstring buf2[4];
			for (int n = 0; n < 4; n++) {
				if (std::wstring::npos == common::WStringSplit(&t2, L':', &buf2[n]))
					break;
			}
			std::wstring genSpace = common::WStringToUpperCase(buf2[0]);
			unsigned int offset = common::WStringToLong(buf2[1]);
			unsigned int count = common::WStringToLong(buf2[2]);
			if (count < 1) {
				count = 999;
			}
			unsigned int relativeTS = 0;
			std::wstring postfix = L"/TS%d";
			if (genSpace == L"VHF-L") {
				Generate.Space = eChGenerateVHF_L;
				Generate.Offset = min(offset, 3UL - 1UL);
				Generate.Count = min(count, 3UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"%dch";
				Generate.NameOffset = 1UL;
			}
			else if (genSpace == L"VHF-H") {
				Generate.Space = eChGenerateVHF_H;
				Generate.Offset = min(offset, 9UL - 1UL);
				Generate.Count = min(count, 9UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"%dch";
				Generate.NameOffset = 4UL;
			}
			else if (genSpace == L"VHF-4+") {
				Generate.Space = eChGenerateVHF_4Plus;
				Generate.Offset = min(offset, 4UL - 1UL);
				Generate.Count = min(count, 4UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"%dch+";
				Generate.NameOffset = 4UL;
			}
			else if (genSpace == L"UHF") {
				Generate.Space = eChGenerateUHF;
				Generate.Offset = min(offset, 50UL - 1UL);
				Generate.Count = min(count, 50UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"%dch";
				Generate.NameOffset = 13UL;
			}
			else if (genSpace == L"CATV-L") {
				Generate.Space = eChGenerateCATV_L;
				Generate.Offset = min(offset, 10UL - 1UL);
				Generate.Count = min(count, 10UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"C%dch";
				Generate.NameOffset = 13UL;
			}
			else if (genSpace == L"CATV-22+") {
				Generate.Space = eChGenerateCATV_22Plus;
				Generate.Offset = min(offset, 1UL - 1UL);
				Generate.Count = min(count, 1UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"C%dch+";
				Generate.NameOffset = 22UL;
			}
			else if (genSpace == L"CATV-H") {
				Generate.Space = eChGenerateCATV_H;
				Generate.Offset = min(offset, 41UL - 1UL);
				Generate.Count = min(count, 41UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"C%dch";
				Generate.NameOffset = 23UL;
			}
			else if (genSpace == L"CATV-24+") {
				Generate.Space = eChGenerateCATV_24Plus;
				Generate.Offset = min(offset, 4UL - 1UL);
				Generate.Count = min(count, 4UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"C%dch+";
				Generate.NameOffset = 24UL;
			}
			else if (genSpace == L"BS1") {
				Generate.Space = eChGenerateBS1;
				Generate.Offset = min(offset, 12UL - 1UL);
				Generate.Count = min(count, 12UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.SatelliteNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"BS%02d/TS%d";
				postfix = L"";
				Generate.NameOffset = 1UL;
				Generate.NameStep = 2UL;
				Generate.NameOffsetTS = 0UL;
				relativeTS = 4UL;
			}
			else if (genSpace == L"ND2") {
				Generate.Space = eChGenerateND2;
				Generate.Offset = min(offset, 12UL - 1UL);
				Generate.Count = min(count, 12UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.SatelliteNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"ND%02d/TS%d";
				postfix = L"";
				Generate.NameOffset = 2UL;
				Generate.NameStep = 2UL;
				Generate.NameOffsetTS = 0UL;
			}
			else if (genSpace == L"JD17A") {
				Generate.Space = eChGenerateJD17A;
				Generate.Offset = min(offset, 12UL - 1UL);
				Generate.Count = min(count, 12UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.SatelliteNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"JD%d";
				Generate.NameOffset = 17UL;
			}
			else if (genSpace == L"JD17B") {
				Generate.Space = eChGenerateJD17B;
				Generate.Offset = min(offset, 16UL - 1UL);
				Generate.Count = min(count, 16UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.SatelliteNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"JD%d";
				Generate.NameOffset = 17UL;
			}
			else if (genSpace == L"JD1") {
				Generate.Space = eChGenerateJD1;
				Generate.Offset = min(offset, 16UL - 1UL);
				Generate.Count = min(count, 16UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.SatelliteNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"JD%d";
				Generate.NameOffset = 1UL;
			}
			else if (genSpace == L"OPTICAST") {
				Generate.Space = eChGenerateOpticast;
				Generate.Offset = min(offset, 58UL - 1UL);
				Generate.Count = min(count, 58UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"H%03d";
				Generate.NameOffset = 1UL;
			}
			else if (genSpace == L"OPTICAST-11+") {
				Generate.Space = eChGenerateOpticast_11Plus;
				Generate.Offset = min(offset, 2UL - 1UL);
				Generate.Count = min(count, 2UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"H%03d+";
				Generate.NameOffset = 11UL;
			}
			else if (genSpace == L"OPTICAST-26+") {
				Generate.Space = eChGenerateOpticast_26Plus;
				Generate.Offset = min(offset, 3UL - 1UL);
				Generate.Count = min(count, 3UL - offset);
				Generate.ModulationNumber = 0UL;
				Generate.TuningFreq = 0UL;
				Generate.NameFormat = L"H%03d+";
				Generate.NameOffset = 26UL;
			}
			// TS
			if (buf2[3] != L"") {
				Generate.RelativeTS = common::WStringToLong(buf2[3]);
				Generate.NameFormat += postfix;
			}
			else {
				Generate.RelativeTS = relativeTS;
			}

			// ﾏ?
			if (buf[1] != L"") {
				Generate.ModulationNumber = common::WStringToLong(buf[1]);
			}
			// q
			if (buf[2] != L"") {
				Generate.SatelliteNumber = common::WStringToLong(buf[2]);
			}
			// 謫ｪ`l
			if (buf[3] != L"") {
				Generate.StartCh = common::WStringToLong(buf[3]);
			}
			else {
				Generate.StartCh = nextCh;
			}
			// `[jOgItZbg
			if (buf[4] != L"") {
				Generate.TuningFreq = common::WStringToLong(buf[4]);
			}
			// `ltH[}bg
			if (buf[5] != L"") {
				Generate.NameFormat = buf[5];
			}
			// `lItZbg
			if (buf[6] != L"") {
				Generate.NameOffset = common::WStringToLong(buf[6]);
			}
			// `lXebv
			if (buf[7] != L"") {
				Generate.NameStep = common::WStringToLong(buf[7]);
			}
			// TSﾔ巧tZbg
			if (buf[8] != L"") {
				Generate.NameOffsetTS = common::WStringToLong(buf[8]);
			}

			unsigned int freqBase = 0UL;			// 謫ｪ`lﾌ使
			unsigned int freqStep = 0UL;			// gXebv
			unsigned int polarisastionType = 0UL;	// ﾎ波^Cv 0 .. gp, 1 .. V/Hﾌ繰, 8 .. R

			switch (Generate.Space) {
			case eChGenerateVHF_L:
				freqBase = 93000UL;
				freqStep = 6000UL;
				break;
			case eChGenerateVHF_H:
				freqBase = 171000UL;
				freqStep = 6000UL;
				break;
			case eChGenerateVHF_4Plus:
				freqBase = 173000UL;
				freqStep = 6000UL;
				break;
			case eChGenerateUHF:
				freqBase = 473000UL;
				freqStep = 6000UL;
				break;
			case eChGenerateCATV_L:
				freqBase = 111000UL;
				freqStep = 6000UL;
				break;
			case eChGenerateCATV_22Plus:
				freqBase = 167000UL;
				freqStep = 6000UL;
				break;
			case eChGenerateCATV_H:
				freqBase = 225000UL;
				freqStep = 6000UL;
				break;
			case eChGenerateCATV_24Plus:
				freqBase = 233000UL;
				freqStep = 6000UL;
				break;
			case eChGenerateBS1:
				freqBase = 11727480UL;
				freqStep = 38360UL;
				polarisastionType = 8UL;
				break;
			case eChGenerateND2:
				freqBase = 12291000UL;
				freqStep = 40000UL;
				polarisastionType = 8UL;
				break;
			case eChGenerateJD1:
				freqBase = 12508000UL;
				freqStep = 15000UL;
				polarisastionType = 1UL;
				break;
			case eChGenerateJD17A:
				freqBase = 12268000UL;
				freqStep = 20000UL;
				polarisastionType = 1UL;
				break;
			case eChGenerateJD17B:
				freqBase = 12268000UL;
				freqStep = 15000UL;
				polarisastionType = 1UL;
				break;
			case eChGenerateOpticast:
				freqBase = 93250UL;
				freqStep = 6500UL;
				break;
			case eChGenerateOpticast_11Plus:
				freqBase = 159250UL;
				freqStep = 6500UL;
				break;
			case eChGenerateOpticast_26Plus:
				freqBase = 259750UL;
				freqStep = 6500UL;
				break;
			}

			if (Generate.Count && freqBase) {
				bReserveUnusedCh = TRUE;
				unsigned int tsCount = max(Generate.RelativeTS, 1);
				unsigned int chNum = Generate.StartCh;
				for (unsigned int ch = Generate.Offset; ch < Generate.Offset + Generate.Count; ch++) {
					for (unsigned int ts = 0; ts < tsCount; ts++) {
						auto itCh = itSpace->second.Channels.find(chNum);
						if (itCh == itSpace->second.Channels.end()) {
							itCh = itSpace->second.Channels.emplace(chNum, ChData()).first;
						}
						else {
							OutputDebug(L"    Replaced to :\n");
						}
						switch (polarisastionType) {
						case 1UL:
							itCh->second.Polarisation = (ch % 2UL) ? 1UL : 2UL;
							break;
						case 8UL:
							itCh->second.Polarisation = 4UL;
							break;
						default:
							itCh->second.Polarisation = 0UL;
							break;
						}
						itCh->second.ModulationType = Generate.ModulationNumber;
						itCh->second.Satellite = Generate.SatelliteNumber;
						itCh->second.Frequency = freqBase + freqStep * ch + Generate.TuningFreq;
						if (Generate.RelativeTS) {
							itCh->second.TSID = ts;
						}
						itCh->second.sServiceName = common::TStringPrintf(common::WStringToTString(Generate.NameFormat).c_str(), ch * Generate.NameStep + Generate.NameOffset, ts + Generate.NameOffsetTS);
						OutputDebug(L"%s: (auto) CH%03ld=%ld,%ld.%03ld,%c,%ld,%s,%ld,%ld,%ld,%ld,%ld\n", section.c_str(), itCh->first, itCh->second.Satellite, itCh->second.Frequency / 1000L,
							itCh->second.Frequency % 1000L, PolarisationChar[itCh->second.Polarisation], itCh->second.ModulationType, itCh->second.sServiceName.c_str(), itCh->second.SID, 
							itCh->second.TSID, itCh->second.ONID, itCh->second.MajorChannel, itCh->second.SourceID);
						chNum++;
						nextCh = max(nextCh, chNum);
					}
				}
				OutputDebug(L".\n");
			}
		}

		// gItZbgl
		itSpace->second.FrequencyOffset = (long)IniFileAccess.ReadKeyISectionData(L"FrequencyOffset", 0);

		// TuningSpaceﾌ酌?
		itSpace->second.DVBSystemTypeNumber = IniFileAccess.ReadKeyISectionData(L"DVBSystemTypeNumber", 0);

		// TSMFﾌ充h
		itSpace->second.TSMFMode = IniFileAccess.ReadKeyIValueMapSectionData(L"TSMFMode", 0, mapTSMFMode);

		// CH
		//    `l = q,g,ﾎ波,ﾏ端,`l[,SID/MinorChannel[,TSID/Channel[,ONID/PhysicalChannel[,MajorChannel[,SourceID]]]]]]
		//    : CH001 = 1,12658,V,0
		//      `l : CH000`CH999ﾅ指
		//      q       : SatelliteZNVﾅ設定しq(1`4)  0(w)
		//                       (nfW`[i[0w閧ｵ)
		//      g         : gMHzﾅ指
		//                       (_t驍ｱﾆ?KHzPﾊでの指閧ｪﾂ能)
		//      ﾎ波           : 'V' = ﾎ波 'H'=ﾎ波 'L'=~ﾎ波 'R'=E~ﾎ波 ' '=w
		//                       (nfW`[i[ﾍ殆)
		//             : ModulationZNVﾅ設定しﾏ?(0`3)
		//      `l   : `l
		//                       (ﾈ隷合 128.0E / 12658H / DVB - S ﾌようﾈ形ﾅ?)
		//      SID            : DVB / ISDBﾌサ[rXID
		//      TSID           : DVB / ISDBﾌトX|[gXg[ID
		//      ONID           : DVB / ISDBﾌオWilbg[NID
		//      MinorChannel   : ATSC / Digital CableMinor Channel
		//      Channel        : ATSC / Digital CableChannel
		//      PhysicalChannel: ATSC / Digital CablePhysical Channel
		//      MajorChannel   : Digital CableMajor Channel
		//      SourceID       : Digital CableSourceID
		std::wstring key, data;
		while (IniFileAccess.ReadSectionData(&key, &data) == true) {
			std::wregex re(LR"(^CH(?:\d{3}|)$)", std::regex_constants::icase);
			if (std::regex_match(key, re) == true) {
				if (data.length() != 0) {
					// CHﾝ鱈
					DWORD ch = common::WStringDecimalToLong(key.substr(2));

					// ReserveUnusedChw閧ｳﾄれ合CHﾔ阪書
					// w閧ｳﾄなれ合ﾍ登^CHﾔ攻
					DWORD chNum = bReserveUnusedCh ? ch : (DWORD)(itSpace->second.Channels.size());
					auto itCh = itSpace->second.Channels.find(chNum);
					if (itCh == itSpace->second.Channels.end()) {
						itCh = itSpace->second.Channels.emplace(chNum, ChData()).first;
					}

					// J}10ﾂ?
					std::wstring t(data);
					std::wstring buf[10];
					for (int n = 0; n < 10; n++) {
						if (std::wstring::npos == common::WStringSplit(&t, L',', &buf[n]))
							break;
					}

					// q
					val = common::WStringToLong(buf[0]);
					if (val >= 0 && val < MAX_SATELLITE) {
						itCh->second.Satellite = val;
					}
					else
						OutputDebug(L"Format Error in readIniFile; Wrong Bird.\n");

					// g
					val = (int)(common::WstringToDouble(buf[1]) * 1000.0);
					if ((val > 0) && (val <= 30000000)) {
						itCh->second.Frequency = val;
					}
					else
						OutputDebug(L"Format Error in readIniFile; Wrong Frequency.\n");

					// ﾎ波
					WCHAR c = buf[2].c_str()[0];
					if (c == L'\0') {
						c = L' ';
					}
					auto it = std::find(std::begin(PolarisationChar), std::end(PolarisationChar), c);
					if (it != std::end(PolarisationChar)) {
						itCh->second.Polarisation = (unsigned int)(it - std::begin(PolarisationChar));
					}
					else
						OutputDebug(L"Format Error in readIniFile; Wrong Polarisation.\n");

					// 
					val = common::WStringToLong(buf[3]);
					if (val >= 0 && val < MAX_MODULATION) {
						itCh->second.ModulationType = val;
					}
					else
						OutputDebug(L"Format Error in readIniFile; Wrong Method.\n");

					// `l
					std::basic_string<TCHAR> name(common::WStringToTString(buf[4]));
					if (name.length() == 0)
						// init@Cﾅ指閧ｵﾌ?128.0E/12658H/DVB-S ﾌようﾈ形ﾅ作成
						name = MakeChannelName(&itCh->second);
					itCh->second.sServiceName = name;

					// SID / PhysicalChannel
					if (buf[5] != L"") {
						val = common::WStringToLong(buf[5]);
						itCh->second.SID = val;
					}

					// TSID / Channel
					if (buf[6] != L"") {
						val = common::WStringToLong(buf[6]);
						itCh->second.TSID = val;
					}

					// ONID / MinorChannel
					if (buf[7] != L"") {
						val = common::WStringToLong(buf[7]);
						itCh->second.ONID = val;
					}

					// MajorChannel
					if (buf[8] != L"") {
						val = common::WStringToLong(buf[8]);
						itCh->second.MajorChannel = val;
					}

					// SourceID
					if (buf[9] != L"") {
						val = common::WStringToLong(buf[9]);
						itCh->second.SourceID = val;
					}
					OutputDebug(L"%s: (manual) CH%03ld=%ld,%ld.%03ld,%c,%ld,%s,%ld,%ld,%ld,%ld,%ld\n", section.c_str(), itCh->first, itCh->second.Satellite, itCh->second.Frequency / 1000L,
						itCh->second.Frequency % 1000L, PolarisationChar[itCh->second.Polarisation], itCh->second.ModulationType, itCh->second.sServiceName.c_str(), itCh->second.SID,
						itCh->second.TSID, itCh->second.ONID, itCh->second.MajorChannel, itCh->second.SourceID);
				}
			}
		}

		//`l`
		auto itChEnd = itSpace->second.Channels.end();
		if (itChEnd == itSpace->second.Channels.begin()) {
			// `l`1ﾂ?
			itSpace->second.dwNumChannel = 0;
		}
		else {
			// CHﾔ採最鼠 + 1
			itChEnd--;
			itSpace->second.dwNumChannel = itChEnd->first + 1;
		}
		OutputDebug(L"%s: dwNumChannel = %d.\n", section.c_str(), itSpace->second.dwNumChannel);

		// CHﾘ替的2xsﾌ対修H
		if (m_bLockTwice) {
			std::wstring s = IniFileAccess.ReadKeySSectionData(L"ChannelLockTwiceTarget", L"");
			if (s != L"") {
				while (1) {
					// J}ﾘまでの墓得
					std::wstring token;
					std::wstring::size_type pos = common::WStringSplit(&s, L',', &token);
					if (token != L"") {
						DWORD begin = 0;
						DWORD end = itSpace->second.dwNumChannel - 1;
						// '-'ﾘの人
						std::wstring left;
						std::wstring right(token);
						if (std::wstring::npos == common::WStringSplit(&right, L'-', &left)) {
							// "-"L
							begin = end = common::WStringToLong(left);
						}
						else {
							// "-"LL
							if (left != L"") {
								// "-"Lﾌ前ﾉ人
								begin = common::WStringToLong(left);
							}
							if (right != L"") {
								// "-"Lﾌ庫人
								end = common::WStringToLong(right);
							}
						}
						// ﾎ象範?CHFlagSet
						for (DWORD ch = begin; ch <= end; ch++) {
							auto itCh = itSpace->second.Channels.find(ch);
							if (itCh != itSpace->second.Channels.end()) {
								itCh->second.LockTwiceTarget = TRUE;
							}
						}
					}
					if (pos == std::wstring::npos)
						break;
				}
			}
			else {
				// ChannelLockTwiceTargetﾌ指閧ｪﾍべ?CH
				for (auto itCh = itSpace->second.Channels.begin(); itCh != itSpace->second.Channels.end(); itCh++) {
					itCh->second.LockTwiceTarget = TRUE;
				}
			}
		}
	}

	// `[jOﾔ?0T
	auto itSpace0 = m_TuningData.Spaces.find(0);
	if (itSpace0 == m_TuningData.Spaces.end()) {
		// ﾉは諒はど一応
		// TuningSpaceData`[jOﾔ?0ﾉ挿
		itSpace0 = m_TuningData.Spaces.emplace(0, TuningSpaceData()).first;
	}

	if (!itSpace0->second.Channels.size()) {
		// CH`ﾂて?
		if (m_nDefaultNetwork == eDefaultNetworkSPHD) {
			// SPHDﾌ場合ﾌみ過粁バ[W
			// 3ﾂ５PftHgﾅセbg
			//   128.0E 12.658GHz V DVB-S *** 2015-10-10ﾝ、NITﾉは貸るけﾇ暖
			auto itCh = itSpace0->second.Channels.emplace(0, ChData()).first;
			itCh->second.Satellite = 1;
			itCh->second.Polarisation = 2;
			itCh->second.ModulationType = 0;
			itCh->second.Frequency = 12658000;
			itCh->second.sServiceName = MakeChannelName(&itCh->second);
			//   124.0E 12.613GHz H DVB-S2
			itCh = itSpace0->second.Channels.emplace(1, ChData()).first;
			itCh->second.Satellite = 2;
			itCh->second.Polarisation = 1;
			itCh->second.ModulationType = 1;
			itCh->second.Frequency = 12613000;
			itCh->second.sServiceName = MakeChannelName(&itCh->second);
			//   128.0E 12.733GHz H DVB-S2
			itCh = itSpace0->second.Channels.emplace(2, ChData()).first;
			itCh->second.Satellite = 1;
			itCh->second.Polarisation = 1;
			itCh->second.ModulationType = 1;
			itCh->second.Frequency = 12733000;
			itCh->second.sServiceName = MakeChannelName(&itCh->second);
			itSpace0->second.dwNumChannel = 3;
		}
	}

	// `[jOﾔ?
	auto itSpaceEnd = m_TuningData.Spaces.end();
	if (itSpaceEnd == m_TuningData.Spaces.begin()) {
		// 
		m_TuningData.dwNumSpace = 0;
	}
	else {
		itSpaceEnd--;
		m_TuningData.dwNumSpace = itSpaceEnd->first + 1;
	}
}

void CBonTuner::PowerSetOnOpened(void)
{
	if (m_sPowerSetOnOpenedGUID.empty())
		return;

	// Null DACLﾌセLeBLqq (TODO: Null DACLﾅ?KvﾈアNZXﾍ囲に珪)
	SECURITY_DESCRIPTOR sdNull = {};
	if (!::InitializeSecurityDescriptor(&sdNull, SECURITY_DESCRIPTOR_REVISION) ||
			!::SetSecurityDescriptorDacl(&sdNull, TRUE, NULL, FALSE)) {
		OutputDebug(L"Security descriptor creation failed.\n");
		return;
	}
	SECURITY_ATTRIBUTES saNull = {};
	saNull.nLength = sizeof(saNull);
	saNull.lpSecurityDescriptor = &sdNull;

	// APIVistaﾈ降
	DWORD (WINAPI *funcPowerSetActiveScheme)(HKEY, const GUID *) = NULL;
	HMODULE hModule = ::LoadLibraryW(L"powrprof.dll");
	if (hModule)
		funcPowerSetActiveScheme = (DWORD (WINAPI *)(HKEY, const GUID *))::GetProcAddress(hModule, "PowerSetActiveScheme");

	if (!funcPowerSetActiveScheme) {
		OutputDebug(L"PowerSetActiveScheme API not found.\n");
		if (hModule)
			::FreeLibrary(hModule);
		return;
	}

	m_hPowerSetLock = ::CreateMutexW(&saNull, FALSE, POWER_SET_LOCK_NAME);
	if (m_hPowerSetLock && ::WaitForSingleObject(m_hPowerSetLock, POWER_SET_WAIT_MSEC) == WAIT_OBJECT_0) {
		BOOL bRet = FALSE;
		// ﾌオuWFNgﾍ最庫閉どＮ驍ｽﾟの単ﾈＵO
		m_hPowerSetFlag = ::CreateMutexW(&saNull, FALSE, POWER_SET_FLAG_NAME);
		if (m_hPowerSetFlag) {
			// IuWFNg�ｶ惜?
			GUID guid;
			bRet = ::UuidFromStringW((RPC_WSTR)m_sPowerSetOnOpenedGUID.c_str(), &guid) == RPC_S_OK &&
				funcPowerSetActiveScheme(NULL, &guid) == 0;
		}
		::ReleaseMutex(m_hPowerSetLock);
		OutputDebug(L"PowerSetActiveScheme(%s) %s.\n", m_sPowerSetOnOpenedGUID.c_str(), bRet ? L"success" : L"failed");
	}
	else {
		OutputDebug(L"Cannot acquire %s.\n", POWER_SET_LOCK_NAME);
	}

	::FreeLibrary(hModule);
}

void CBonTuner::PowerSetOnClosing(void)
{
	if (!m_hPowerSetLock)
		return;

	// APIVistaﾈ降
	DWORD (WINAPI *funcPowerSetActiveScheme)(HKEY, const GUID *) = NULL;
	HMODULE hModule = ::LoadLibraryW(L"powrprof.dll");
	if (hModule)
		funcPowerSetActiveScheme = (DWORD (WINAPI *)(HKEY, const GUID *))::GetProcAddress(hModule, "PowerSetActiveScheme");

	if (m_hPowerSetFlag) {
		if (::WaitForSingleObject(m_hPowerSetLock, POWER_SET_WAIT_MSEC) == WAIT_OBJECT_0) {
			BOOL bRet = FALSE;
			BOOL bLog = FALSE;
			SAFE_CLOSE_HANDLE(m_hPowerSetFlag);
			m_hPowerSetFlag = ::OpenMutexW(SYNCHRONIZE, FALSE, POWER_SET_FLAG_NAME);
			if (!m_hPowerSetFlag) {
				// IuWFNgj
				if (!m_sPowerSetOnClosingGUID.empty()) {
					GUID guid;
					bRet = ::UuidFromStringW((RPC_WSTR)m_sPowerSetOnClosingGUID.c_str(), &guid) == RPC_S_OK &&
						funcPowerSetActiveScheme &&
						funcPowerSetActiveScheme(NULL, &guid) == 0;
					bLog = TRUE;
				}
			}
			SAFE_CLOSE_HANDLE(m_hPowerSetFlag);
			::ReleaseMutex(m_hPowerSetLock);
			if (bLog)
				OutputDebug(L"PowerSetActiveScheme(%s) %s.\n", m_sPowerSetOnClosingGUID.c_str(), bRet ? L"success" : L"failed");
		}
		else {
			OutputDebug(L"Cannot acquire %s.\n", POWER_SET_LOCK_NAME);
			SAFE_CLOSE_HANDLE(m_hPowerSetFlag);
		}
	}

	if (hModule)
		::FreeLibrary(hModule);

	SAFE_CLOSE_HANDLE(m_hPowerSetLock);
}

void CBonTuner::GetSignalState(int* pnStrength, int* pnQuality, int* pnLock)
{
	if (pnStrength) *pnStrength = 0;
	if (pnQuality) *pnQuality = 0;
	if (pnLock) *pnLock = 1;

	// `[iﾅ有 GetSignalState ﾎ、
	HRESULT hr;
	if ((m_pIBdaSpecials) && (hr = m_pIBdaSpecials->GetSignalState(pnStrength, pnQuality, pnLock)) != E_NOINTERFACE) {
		// E_NOINTERFACE ﾅなば、ﾅ有ﾖ脆なので、
		// ﾌままタ[
		return;
	}

	if (m_pTunerDevice == NULL)
		return;

	long longVal;
	BYTE byteVal;

	if (m_pITuner) {
		if ((m_bSignalLevelGetTypeTuner && ((m_bSignalLevelNeedStrength && pnStrength) || (m_bSignalLevelNeedQuality && pnQuality))) ||
				(m_bSignalLockedJudgeTypeTuner && pnLock)) {
			longVal = 0;
			if (SUCCEEDED(hr = m_pITuner->get_SignalStrength(&longVal))) {
				__int16 strength = (__int16)(longVal & 0xffffL);
				__int16 quality = (__int16)(longVal >> 16);
				if (m_bSignalLevelNeedStrength && pnStrength)
					*pnStrength = (int)(strength < 0xffffi16 ? 0xffffi16 - strength : strength);
				if (m_bSignalLevelNeedQuality && pnQuality)
					*pnQuality = min(max((int)quality, 0), 100);
				if (m_bSignalLockedJudgeTypeTuner && pnLock)
					*pnLock = strength != 0i16 ? 1 : 0;
			}
		}
	}

	if (m_pIBDA_SignalStatisticsTunerNode) {
		if (m_bSignalLevelGetTypeSS) {
			if (m_bSignalLevelNeedStrength && pnStrength) {
				longVal = 0;
				if (SUCCEEDED(hr = m_pIBDA_SignalStatisticsTunerNode->get_SignalStrength(&longVal)))
					*pnStrength = (int)(longVal & 0xffff);
			}

			if (m_bSignalLevelNeedQuality && pnQuality) {
				longVal = 0;
				if (SUCCEEDED(hr = m_pIBDA_SignalStatisticsTunerNode->get_SignalQuality(&longVal)))
					*pnQuality = (int)(min(max(longVal & 0xffff, 0), 100));
			}
		}

		if (m_bSignalLockedJudgeTypeSS && pnLock) {
			byteVal = 0;
			if (SUCCEEDED(hr = m_pIBDA_SignalStatisticsTunerNode->get_SignalLocked(&byteVal)))
				*pnLock = (int)byteVal;
		}
	}

	if (m_pIBDA_SignalStatisticsDemodNode) {
		if (m_bSignalLevelGetTypeDemodSS) {
			if (m_bSignalLevelNeedStrength && pnStrength) {
				longVal = 0;
				if (SUCCEEDED(hr = m_pIBDA_SignalStatisticsDemodNode->get_SignalStrength(&longVal)))
					*pnStrength = (int)(longVal & 0xffff);
			}

			if (m_bSignalLevelNeedQuality && pnQuality) {
				longVal = 0;
				if (SUCCEEDED(hr = m_pIBDA_SignalStatisticsDemodNode->get_SignalQuality(&longVal)))
					*pnQuality = (int)(min(max(longVal & 0xffff, 0), 100));
			}
		}

		if (m_bSignalLockedJudgeTypeDemodSS && pnLock) {
			byteVal = 0;
			if (SUCCEEDED(hr = m_pIBDA_SignalStatisticsDemodNode->get_SignalLocked(&byteVal)))
				*pnLock = (int)byteVal;
		}
	}

	{
		static int s_sigLogCount = 0;
		if (s_sigLogCount < 5) {
			s_sigLogCount++;
			OutputDebug(L"  [GetSignalState] strength=%d quality=%d lock=%d\n",
				pnStrength ? *pnStrength : -999,
				pnQuality ? *pnQuality : -999,
				pnLock ? *pnLock : -999);
		}
	}

	return;
}

BOOL CBonTuner::LockChannel(const TuningParam *pTuningParam, BOOL bLockTwice)
{
	HRESULT hr;

	// `[iﾅ有 LockChannel ﾎ、
	if (m_pIBdaSpecials2 && (hr = m_pIBdaSpecials2->LockChannel(pTuningParam)) != E_NOINTERFACE) {
		// BonDriver_BDApDLL
		// E_NOINTERFACE ﾅなば、ﾅ有ﾖ脆なので、
		// ﾌ椎選ﾇ峻ﾈて?Bﾄのままタ[
		m_nCurTone = pTuningParam->Antenna.Tone;
		if (SUCCEEDED(hr) && bLockTwice) {
			OutputDebug(L"  TwiceLock 1st[Special2] SUCCESS.\n");
			SleepWithMessageLoop(m_nLockTwiceDelay);
			hr = m_pIBdaSpecials2->LockChannel(pTuningParam);
		}
		if (SUCCEEDED(hr)) {
			OutputDebug(L"  LockChannel[Special2] SUCCESS.\n");
			return TRUE;
		} else {
			OutputDebug(L"  LockChannel[Special2] FAIL.\n");
			return FALSE;
		}
	}

	if (m_pIBdaSpecials && (hr = m_pIBdaSpecials->LockChannel(pTuningParam->Antenna.Tone ? 1 : 0, (pTuningParam->Polarisation == BDA_POLARISATION_LINEAR_H) ? TRUE : FALSE, pTuningParam->Frequency / 1000,
			(pTuningParam->Modulation.Modulation == BDA_MOD_NBC_8PSK || pTuningParam->Modulation.Modulation == BDA_MOD_8PSK) ? TRUE : FALSE)) != E_NOINTERFACE) {
		// BonDriver_BDAIWiﾝ灰LL
		// E_NOINTERFACE ﾅなば、ﾅ有ﾖ脆なので、
		// ﾌ椎選ﾇ峻ﾈて?Bﾄのままタ[
		m_nCurTone = pTuningParam->Antenna.Tone;
		if (SUCCEEDED(hr) && bLockTwice) {
			OutputDebug(L"  TwiceLock 1st[Special] SUCCESS.\n");
			SleepWithMessageLoop(m_nLockTwiceDelay);
			hr = m_pIBdaSpecials->LockChannel(pTuningParam->Antenna.Tone ? 1 : 0, (pTuningParam->Polarisation == BDA_POLARISATION_LINEAR_H) ? TRUE : FALSE, pTuningParam->Frequency / 1000,
					(pTuningParam->Modulation.Modulation == BDA_MOD_NBC_8PSK || pTuningParam->Modulation.Modulation == BDA_MOD_8PSK) ? TRUE : FALSE);
		}
		if (SUCCEEDED(hr)) {
			OutputDebug(L"  LockChannel[Special] SUCCESS.\n");
			return TRUE;
		}
		else {
			OutputDebug(L"  LockChannel[Special] FAIL.\n");
			return FALSE;
		}
	}

	// `[iﾅ有g[ﾖ石、ﾅ呼び出
	if (m_pIBdaSpecials2 && (hr = m_pIBdaSpecials2->Set22KHz(pTuningParam->Antenna.Tone)) != E_NOINTERFACE) {
		// BonDriver_BDApDLL
		if (SUCCEEDED(hr)) {
			OutputDebug(L"  Set22KHz[Special2] successfully.\n");
			if (pTuningParam->Antenna.Tone != m_nCurTone) {
				m_nCurTone = pTuningParam->Antenna.Tone;
				if (m_nToneWait) {
					SleepWithMessageLoop(m_nToneWait); // qﾘ替?
				}
			}
		}
		else {
			OutputDebug(L"  Set22KHz[Special2] failed.\n");
			// BDA generic ﾈ鼻ﾅ切へるかﾈので、bZ[Woﾄ、ﾌまま壮
		}
	}
	else if (m_pIBdaSpecials && (hr = m_pIBdaSpecials->Set22KHz(pTuningParam->Antenna.Tone ? 1 : 0)) != E_NOINTERFACE) {
		// BonDriver_BDAIWiﾝ灰LL
		if (SUCCEEDED(hr)) {
			OutputDebug(L"  Set22KHz[Special] successfully.\n");
			if (pTuningParam->Antenna.Tone != m_nCurTone) {
				m_nCurTone = pTuningParam->Antenna.Tone;
				if (m_nToneWait) {
					SleepWithMessageLoop(m_nToneWait); // qﾘ替?
				}
			}
		}
		else {
			OutputDebug(L"  Set22KHz[Special] failed.\n");
			// BDA generic ﾈ鼻ﾅ切へるかﾈので、bZ[Woﾄ、ﾌまま壮
		}
	}
	else {
		// ﾅ有ﾖ席なので、
	}

	// TuningSpaceﾌ酌?
	unsigned int systemTypeNumber = m_TuningData.Spaces[pTuningParam->IniSpaceID].DVBSystemTypeNumber;
	if (!m_DVBSystemTypeDB.IsExist(systemTypeNumber))
		systemTypeNumber = 0;

	// ITuningSpacepF
	//   ITuningSpace  IDVBTuningSpace  IDVBTuningSpace2  IDVBSTuningSpace
	//                 IAnalogTVTuningSpace  IATSCTuningSpace  IDigitalCableTuningSpace
	//                 IAnalogRadioTuningSpace  IAnalogRadioTuningSpace2
	//                 IAuxInTuningSpace  IAuxInTuningSpace2
	CComPtr<ITuningSpace> pITuningSpace(m_DVBSystemTypeDB.SystemType[systemTypeNumber].pITuningSpace);
	if (!pITuningSpace) {
		OutputDebug(L"  Fail to get ITuningSpace.\n");
		return FALSE;
	}

	OutputDebug(L"    ITuningSpace\n");
	// IDVBSTuningSpaceL
	{
		CComQIPtr<IDVBSTuningSpace> pIDVBSTuningSpace(pITuningSpace);
		if (pIDVBSTuningSpace) {
			OutputDebug(L"    ->IDVBSTuningSpace\n");
			// LNB g
			if (pTuningParam->Antenna.HighOscillator != -1) {
				pIDVBSTuningSpace->put_HighOscillator(pTuningParam->Antenna.HighOscillator);
			}
			if (pTuningParam->Antenna.LowOscillator != -1) {
				pIDVBSTuningSpace->put_LowOscillator(pTuningParam->Antenna.LowOscillator);
			}

			// LNBXCb`ﾌ使
			if (pTuningParam->Antenna.LNBSwitch != -1) {
				// LNBSwitchgﾌ設定が
				pIDVBSTuningSpace->put_LNBSwitch(pTuningParam->Antenna.LNBSwitch);
			}
			else {
				// 10GHzﾝ定しHighﾉ、20GHzﾝ定しLowﾉ切替?
				pIDVBSTuningSpace->put_LNBSwitch((pTuningParam->Antenna.Tone != 0) ? 10000000 : 20000000);
			}

			// ﾊ耐湛yNg]
			pIDVBSTuningSpace->put_SpectralInversion(BDA_SPECTRAL_INVERSION_AUTOMATIC);
		}
	}

	// ILocator謫ｾ
	//
	// ILocatorpF
	//   ILocator  IDigitalLocator  IDVBTLocator  IDVBTLocator2
	//                                IDVBSLocator  IDVBSLocator2
	//                                                IISDBSLocator
	//                                IDVBCLocator
	//                                IATSCLocator  IATSCLocator2  IDigitalCableLocator
	//             IAnalogLocator
	CComPtr<ILocator> pILocator;
	if (FAILED(hr = pITuningSpace->get_DefaultLocator(&pILocator))) {
		OutputDebug(L"  Fail to get ILocator. hr=0x%08lx\n", hr);
		return FALSE;
	}

	OutputDebug(L"    ILocator\n");
	// RF Mﾌ使
	long freqToSet = pTuningParam->Frequency;
	int freqMul = GetPrivateProfileInt(L"Tuner", L"FrequencyMultiplier", 1, m_sIniFilePath.c_str());
	freqToSet *= freqMul;
	OutputDebug(L"    put_CarrierFrequency(%ld) [multiplier=%d]\n", freqToSet, freqMul);
	pILocator->put_CarrierFrequency(freqToSet);
	if (m_pIBDA_FrequencyFilter) {
		HRESULT hrFF = m_pIBDA_FrequencyFilter->put_Frequency(freqToSet);
		OutputDebug(L"  [TunerNode] Direct m_pIBDA_FrequencyFilter->put_Frequency(%ld): hr=0x%08X\n", freqToSet, hrFF);
	}

	// OﾌタCv
	pILocator->put_InnerFEC(pTuningParam->Modulation.InnerFEC);

	//  FEC [g
	// Oﾅ使oCi R{[Vﾌコ[h [g DVB-S 3/4 S2 3/5
	pILocator->put_InnerFECRate(pTuningParam->Modulation.InnerFECRate);

	// ﾏ耽Cv
	// DVB-SQPSKAS2ﾌ場合 8PSK
	pILocator->put_Modulation(pTuningParam->Modulation.Modulation);

	// OOﾌタCv
	//	[h-\ 204/188 (O FEC), DVB-S2
	pILocator->put_OuterFEC(pTuningParam->Modulation.OuterFEC);

	// O FEC [g
	pILocator->put_OuterFECRate(pTuningParam->Modulation.OuterFECRate);

	// QPSK V{ [g
	pILocator->put_SymbolRate(pTuningParam->Modulation.SymbolRate);

	// IDVBSLocatorL
	{
		CComQIPtr<IDVBSLocator> pIDVBSLocator(pILocator);
		if (pIDVBSLocator) {
			OutputDebug(L"    ->IDVBSLocator\n");
			// Mﾌ偏波
			pIDVBSLocator->put_SignalPolarisation(pTuningParam->Polarisation);
		}
	}

	// IDVBSLocator2L
	{
		CComQIPtr<IDVBSLocator2> pIDVBSLocator2(pILocator);
		if (pIDVBSLocator2) {
			OutputDebug(L"    ->IDVBSLocator2\n");
			// DiSEqC
			if (pTuningParam->Antenna.DiSEqC >= BDA_LNB_SOURCE_A) {
				pIDVBSLocator2->put_DiseqLNBSource((LNB_Source)(pTuningParam->Antenna.DiSEqC));
			}
			else {
				pIDVBSLocator2->put_DiseqLNBSource(BDA_LNB_SOURCE_NOT_SET);
			}
		}
	}

	// IDVBTLocatorL
	{
		CComQIPtr<IDVBTLocator> pIDVBTLocator(pILocator);
		if (pIDVBTLocator) {
			OutputDebug(L"    ->IDVBTLocator\n");
			// gﾌ帯域幅 (MHz)
			if (pTuningParam->Modulation.BandWidth != -1) {
				pIDVBTLocator->put_Bandwidth(pTuningParam->Modulation.BandWidth);
			}
		}
	}

	// IATSCLocatorL
	{
		CComQIPtr<IATSCLocator> pIATSCLocator(pILocator);
		if (pIATSCLocator) {
			OutputDebug(L"    ->IATSCLocator\n");
			// ATSC PhysicalChannel
			if (pTuningParam->PhysicalChannel != -1) {
				pIATSCLocator->put_PhysicalChannel(pTuningParam->PhysicalChannel);
			}
		}
	}

	// ITuneRequest
	//
	// ITuneRequestpF
	//   ITuneRequest  IDVBTuneRequest
	//                 IChannelTuneRequest  IATSCChannelTuneRequest  IDigitalCableTuneRequest
	//                 IChannelIDTuneRequest
	//                 IMPEG2TuneRequest
	CComPtr<ITuneRequest> pITuneRequest;
	if (FAILED(hr = pITuningSpace->CreateTuneRequest(&pITuneRequest))) {
		OutputDebug(L"  Fail to create ITuneRequest. hr=0x%08lx\n", hr);
		return FALSE;
	}

	OutputDebug(L"    ITuneRequest\n");
	// ITuneRequestILocator
	hr = pITuneRequest->put_Locator(pILocator);

	// IDVBTuneRequestL
	{
		CComQIPtr<IDVBTuneRequest> pIDVBTuneRequest(pITuneRequest);
		if (pIDVBTuneRequest) {
			OutputDebug(L"    ->IDVBTuneRequest\n");
			// DVB Triplet IDﾌ?
			pIDVBTuneRequest->put_ONID(pTuningParam->ONID);
			pIDVBTuneRequest->put_TSID(pTuningParam->TSID);
			pIDVBTuneRequest->put_SID(pTuningParam->SID);
		}
	}

	// IChannelTuneRequestL
	{
		CComQIPtr<IChannelTuneRequest> pIChannelTuneRequest(pITuneRequest);
		if (pIChannelTuneRequest) {
			OutputDebug(L"    ->IChannelTuneRequest\n");
			// ATSC Channel
			pIChannelTuneRequest->put_Channel(pTuningParam->Channel);
		}
	}

	// IATSCChannelTuneRequestL
	{
		CComQIPtr<IATSCChannelTuneRequest> pIATSCChannelTuneRequest(pITuneRequest);
		if (pIATSCChannelTuneRequest) {
			OutputDebug(L"    ->IATSCChannelTuneRequest\n");
			// ATSC MinorChannel
			pIATSCChannelTuneRequest->put_MinorChannel(pTuningParam->MinorChannel);
		}
	}

	// IDigitalCableTuneRequestL
	{
		CComQIPtr<IDigitalCableTuneRequest> pIDigitalCableTuneRequest(pITuneRequest);
		if (pIDigitalCableTuneRequest) {
			OutputDebug(L"    ->IDigitalCableTuneRequest\n");
			// Digital Cable MinorChannel
			pIDigitalCableTuneRequest->put_MajorChannel(pTuningParam->MinorChannel);
			// Digital Cable SourceID
			pIDigitalCableTuneRequest->put_SourceID(pTuningParam->SourceID);
		}
	}

	if (m_pIBdaSpecials2) {
		// `[iﾅ有TSIDﾖ石呼び出
		hr = m_pIBdaSpecials2->SetTSid(pTuningParam->TSID);

		// m_pIBdaSpecialsput_TuneRequestﾌ前ﾉ臥かﾌ習vﾈＴ
		hr = m_pIBdaSpecials2->PreTuneRequest(pTuningParam, pITuneRequest);
	}

	if (pTuningParam->Antenna.Tone != m_nCurTone && m_nToneWait) {
		//g[ﾘ替の場合AxTuneRequest
		OutputDebug(L"  Requesting pre tune.\n");
		if (FAILED(hr = m_pITuner->put_TuneRequest(pITuneRequest))) {
			OutputDebug(L"  Fail to put pre tune request.  hr=0x%08lx\n", hr);
			return FALSE;
		}
		OutputDebug(L"  Pre tune request complete.\n");
		if (m_pIBdaSpecials2) {
			// m_pIBdaSpecialsput_TuneRequestﾌ庫臥かﾌ習vﾈＴ
			hr = m_pIBdaSpecials2->PostTuneRequest(pTuningParam);
		}

		SleepWithMessageLoop(m_nToneWait); // qﾘ替?
	}
	m_nCurTone = pTuningParam->Antenna.Tone;

	if (bLockTwice) {
		// TuneRequestI2xs
		OutputDebug(L"  Requesting 1st twice tune.\n");
		if (FAILED(hr = m_pITuner->put_TuneRequest(pITuneRequest))) {
			OutputDebug(L"  Fail to put 1st twice tune request. hr=0x%08lx\n", hr);
			return FALSE;
		}
		OutputDebug(L"  1st Twice tune request complete.\n");
		if (m_pIBdaSpecials2) {
			// m_pIBdaSpecialsput_TuneRequestﾌ庫臥かﾌ習vﾈＴ
			hr = m_pIBdaSpecials2->PostTuneRequest(pTuningParam);
		}
		SleepWithMessageLoop(m_nLockTwiceDelay);
	}

	unsigned int nRetryRemain = m_nLockWaitRetry;
	int nLock = 0;
	do {
		OutputDebug(L"  Requesting tune.\n");
		if (FAILED(hr = m_pITuner->put_TuneRequest(pITuneRequest))) {
			OutputDebug(L"  Fail to put tune request. hr=0x%08lx\n", hr);
			return FALSE;
		}
		OutputDebug(L"  Tune request complete.\n");
		if (m_pTunerDevice) {
			CComQIPtr<IBDA_DeviceControl> pDevCtrl(m_pTunerDevice);
			if (pDevCtrl) {
				HRESULT hrCC = pDevCtrl->CommitChanges();
				OutputDebug(L"  [BDA] CommitChanges after Tune returned 0x%08X\n", hrCC);
			}
		}
		OutputDebug(L"  Waiting 1000ms for hardware demodulator lock...\n");
		SleepWithMessageLoop(1000);
		if (m_bAlwaysAnswerLocked) {
			nLock = 1;
			break;
		}
		if (m_pIBdaSpecials2) {
			// m_pIBdaSpecialsput_TuneRequestﾌ庫臥かﾌ習vﾈＴ
			hr = m_pIBdaSpecials2->PostTuneRequest(pTuningParam);
		}

		static constexpr int LockRetryTime = 50;
		unsigned int nWaitRemain = m_nLockWait;
		SleepWithMessageLoop(m_nLockWaitDelay);
		GetSignalState(NULL, NULL, &nLock);
		while (!nLock && nWaitRemain) {
			DWORD dwSleepTime = (nWaitRemain > LockRetryTime) ? LockRetryTime : nWaitRemain;
			OutputDebug(L"    Waiting lock status remaining %d msec.\n", nWaitRemain);
			SleepWithMessageLoop(dwSleepTime);
			nWaitRemain -= dwSleepTime;
			GetSignalState(NULL, NULL, &nLock);
		}
	} while (!nLock && nRetryRemain--);

	if (nLock != 0)
		OutputDebug(L"  LockChannel success.\n");
	else
		OutputDebug(L"  LockChannel failed.\n");

	return nLock != 0;
}

// `[iﾅ有Dllﾌゼh
HRESULT CBonTuner::CheckAndInitTunerDependDll(IBaseFilter * pTunerDevice, std::wstring tunerGUID, std::wstring tunerFriendlyName)
{
	if (m_aTunerParam.sDLLBaseName == L"") {
		// `[iﾅ有ﾖ身ﾈれ合
		return S_OK;
	}

	if (common::WStringToUpperCase(m_aTunerParam.sDLLBaseName) == L"AUTO") {
		// INI t@C "AUTO" wﾌ場合
		BOOL found = FALSE;
		for (unsigned int i = 0; i < sizeof aTunerSpecialData / sizeof TUNER_SPECIAL_DLL; i++) {
			std::wstring dbGUID(aTunerSpecialData[i].sTunerGUID);
			if ((dbGUID != L"") && (tunerGUID.find(dbGUID)) != std::wstring::npos) {
				// ﾌ捨チ[iﾋ然[h`[ip[^ﾉ変惜セbg
				m_aTunerParam.sDLLBaseName = aTunerSpecialData[i].sDLLBaseName;
				break;
			}
		}
		if (!found) {
			// ﾂなのでチ[iﾅ有ﾖ析使
			return S_OK;
		}
	}

	//  DLL [hB
	WCHAR szPath[_MAX_PATH + 1] = L"";
	::GetModuleFileNameW(st_hModule, szPath, _MAX_PATH + 1);
	// tpX
	WCHAR szDrive[_MAX_DRIVE];
	WCHAR szDir[_MAX_DIR];
	WCHAR szFName[_MAX_FNAME];
	WCHAR szExt[_MAX_EXT];
	::_wsplitpath_s(szPath, szDrive, szDir, szFName, szExt);

	// tH_謫ｾ
	std::wstring sDllName;
	sDllName = common::WStringPrintf(L"%s%s%s.dll", szDrive, szDir, m_aTunerParam.sDLLBaseName.c_str());

	if ((m_hModuleTunerSpecials = ::LoadLibraryW(sDllName.c_str())) == NULL) {
		// [hﾅなれ合A? 
		//   fobObZ[Woﾄ、ﾅ有ﾖ身ﾈのと?
		OutputDebug(L"CheckAndInitTunerDependDll: DLL Not found.\n");
		return S_OK;
	} else {
		OutputDebug(L"CheckAndInitTunerDependDll: Load Library successfully.\n");
	}

	HRESULT (* func)(IBaseFilter *, const WCHAR *, const WCHAR *, const WCHAR *) =
		(HRESULT (*)(IBaseFilter *, const WCHAR *, const WCHAR *, const WCHAR *))::GetProcAddress(m_hModuleTunerSpecials, "CheckAndInitTuner");
	if (!func) {
		// R[h
		// sv
		return S_OK;
	}

	return (* func)(pTunerDevice, tunerGUID.c_str(), tunerFriendlyName.c_str(), m_sIniFilePath.c_str());
}

// `[iﾅ有Dllﾅのキv`foCXmF
HRESULT CBonTuner::CheckCapture(std::wstring tunerGUID, std::wstring tunerFriendlyName, std::wstring captureGUID, std::wstring captureFriendlyName)
{
	if (m_hModuleTunerSpecials == NULL) {
		return S_OK;
	}

	HRESULT (* func)(const WCHAR *, const WCHAR *, const WCHAR *, const WCHAR *, const WCHAR *) =
		(HRESULT (*)(const WCHAR *, const WCHAR *, const WCHAR *, const WCHAR *, const WCHAR *))::GetProcAddress(m_hModuleTunerSpecials, "CheckCapture");
	if (!func) {
		return S_OK;
	}

	return (* func)(tunerGUID.c_str(), tunerFriendlyName.c_str(), captureGUID.c_str(), captureFriendlyName.c_str(), m_sIniFilePath.c_str());
}

// `[iﾅ有ﾖ昔ゼh
void CBonTuner::LoadTunerDependCode(std::wstring tunerGUID, std::wstring tunerFriendlyName, std::wstring captureGUID, std::wstring captureFriendlyName)
{
	if (!m_hModuleTunerSpecials)
		return;

	IBdaSpecials* (*func)(CComPtr<IBaseFilter>);
	func = (IBdaSpecials* (*)(CComPtr<IBaseFilter>))::GetProcAddress(m_hModuleTunerSpecials, "CreateBdaSpecials");
	IBdaSpecials* (*func2)(CComPtr<IBaseFilter>, CComPtr<IBaseFilter>, const WCHAR*, const WCHAR*, const WCHAR*, const WCHAR*);
	func2 = (IBdaSpecials * (*)(CComPtr<IBaseFilter>, CComPtr<IBaseFilter>, const WCHAR*, const WCHAR*, const WCHAR*, const WCHAR*))::GetProcAddress(m_hModuleTunerSpecials, "CreateBdaSpecials2");
	if (!func2 && !func) {
		OutputDebug(L"LoadTunerDependCode: Cannot find CreateBdaSpecials.\n");
		::FreeLibrary(m_hModuleTunerSpecials);
		m_hModuleTunerSpecials = NULL;
		return;
	}
	if (func2)
	{
		OutputDebug(L"LoadTunerDependCode: CreateBdaSpecials2 found.\n");
		m_pIBdaSpecials = func2(m_pTunerDevice, m_pCaptureDevice, tunerGUID.c_str(), tunerFriendlyName.c_str(), captureGUID.c_str(), captureFriendlyName.c_str());
	}
	else {
		OutputDebug(L"LoadTunerDependCode: CreateBdaSpecials found.\n");
		m_pIBdaSpecials = func(m_pTunerDevice);
	}

	m_pIBdaSpecials2 = dynamic_cast<IBdaSpecials2b5 *>(m_pIBdaSpecials);
	if (!m_pIBdaSpecials2)
		OutputDebug(L"LoadTunerDependCode: Not IBdaSpecials2 Interface DLL.\n");

	//  BdaSpecialsinit@Cﾇみ?
	HRESULT hr;
	if (m_pIBdaSpecials2) {
		hr = m_pIBdaSpecials2->ReadIniFile(m_sIniFilePath.c_str());
	}

	// `[iﾅ有ﾖ税市
	if (m_pIBdaSpecials)
		m_pIBdaSpecials->InitializeHook();

	return;
}

// `[iﾅ有ﾖ織ll
void CBonTuner::ReleaseTunerDependCode(void)
{
	HRESULT hr;

	// `[iﾅ有ﾖ秦ﾄば、ﾅ市
	if (m_pIBdaSpecials) {
		if ((hr = m_pIBdaSpecials->FinalizeHook()) == E_NOINTERFACE) {
			// ﾅ有Finalizeﾖ席なので、
		}
		else if (SUCCEEDED(hr)) {
			OutputDebug(L"ReleaseTunerDependCode: Tuner Special Finalize successfully.\n");
		}
		else {
			OutputDebug(L"ReleaseTunerDependCode: Tuner Special Finalize failed.\n");
		}

		SAFE_RELEASE(m_pIBdaSpecials);
		m_pIBdaSpecials2 = NULL;
	}

	if (m_hModuleTunerSpecials) {
		if (::FreeLibrary(m_hModuleTunerSpecials) == 0) {
			OutputDebug(L"ReleaseTunerDependCode: FreeLibrary failed.\n");
		}
		else {
			OutputDebug(L"ReleaseTunerDependCode: FreeLibrary Success.\n");
			m_hModuleTunerSpecials = NULL;
		}
	}
}

HRESULT CBonTuner::InitializeGraphBuilder(void)
{
	HRESULT hr = E_FAIL;
	
	// pIGraphBuilder interface謫ｾ
	CComPtr<IGraphBuilder> pIGraphBuilder;
	if (FAILED(hr = pIGraphBuilder.CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER))) {
		OutputDebug(L"[InitializeGraphBuilder] Fail to get IGraphBuilder interface.\n");
	}
	else {
		// pIGraphBuilder interfaceﾌ取得
		// IMediaControl interface謫ｾ
		CComQIPtr<IMediaControl> pIMediaControl(pIGraphBuilder);
		if (!pIMediaControl) {
			OutputDebug(L"[InitializeGraphBuilder] Fail to get IMediaControl interface.\n");
			hr = E_FAIL;
		}
		else {
			if (m_bRegisterGraphInROT) {
				std::wstring name = common::WStringPrintf(L"FilterGraph %p pid %08x", pIGraphBuilder.p, ::GetCurrentProcessId());
				IRunningObjectTable * pROT;
				if (SUCCEEDED(::GetRunningObjectTable(0, &pROT))) {
					IMoniker * pMoniker;
					if (SUCCEEDED(::CreateItemMoniker(L"!", name.c_str(), &pMoniker))) {
						if (FAILED(pROT->Register(ROTFLAGS_REGISTRATIONKEEPSALIVE, pIGraphBuilder, pMoniker, &m_dwROTRegister)))
							m_dwROTRegister = 0;
						pMoniker->Release();
					}
					pROT->Release();
				}
				if (m_dwROTRegister == 0)
					OutputDebug(L"ROT registration failed.\n");
				else
					OutputDebug(L"ROT registration success. entry=%u (%s).\n", m_dwROTRegister, name.c_str());
			}

			// ﾈのでのまま終
			m_pIGraphBuilder = pIGraphBuilder;
			m_pIMediaControl = pIMediaControl;
			return hr;
		}
	}

	// s
	return hr;
}

void CBonTuner::CleanupGraph(void)
{
	// DisconnectAll(m_pTif);
	// DisconnectAll(m_pDemux);
	// DisconnectAll(m_pTsWriter);
	// DisconnectAll(m_pCaptureDevice);
	// DisconnectAll(m_pTunerDevice);
	// DisconnectAll(m_pNetworkProvider);

	UnloadTif();
	UnloadDemux();
	UnloadTsWriter();
	UnloadSubstreamSink();

	// Tuner  Capture  Release 
	// [NNfoCX
	UnloadTunerDevice();
	UnloadCaptureDevice();

	UnloadNetworkProvider();
	UnloadTuningSpace();

	if (m_dwROTRegister != 0) {
		IRunningObjectTable * pROT;
		if (SUCCEEDED(::GetRunningObjectTable(0, &pROT))) {
			pROT->Revoke(m_dwROTRegister);
			pROT->Release();
		}
		m_dwROTRegister = 0;
	}

	m_pIMediaControl.Release();
	m_pIGraphBuilder.Release();

	return;
}





// DirectShow COM IKsControl definition
#ifndef __IKsControl_INTERFACE_DEFINED__
#define __IKsControl_INTERFACE_DEFINED__
MIDL_INTERFACE("28F54685-06FD-11D2-B27A-00A0C9223196")
IKsControl : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE KsProperty( 
        PKSPROPERTY Property,
        ULONG PropertyLength,
        void *PropertyData,
        ULONG DataLength,
        ULONG *BytesReturned) = 0;
        
    virtual HRESULT STDMETHODCALLTYPE KsMethod( 
        PKSMETHOD Method,
        ULONG MethodLength,
        void *MethodData,
        ULONG DataLength,
        ULONG *BytesReturned) = 0;
        
    virtual HRESULT STDMETHODCALLTYPE KsEvent( 
        PKSEVENT Event,
        ULONG EventLength,
        void *EventData,
        ULONG DataLength,
        ULONG *BytesReturned) = 0;
};
#endif

// GUIDs for GV-MVP/AZ Hardware Through Streaming
static const GUID IID_IKsControl_Local = 
    { 0x28f54685, 0x06fd, 0x11d2, { 0xb2, 0x7a, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
static const GUID KSMETHODSETID_GVMVPAZ_Through = 
    { 0x74395ba9, 0x33a7, 0x4ad4, { 0x86, 0xa7, 0x07, 0x81, 0x49, 0x60, 0x25, 0x9c } };
static const GUID KSPROPSETID_MB86M01_ForcePmtThrough = 
    { 0xcd0b5b68, 0xfdb5, 0x4d61, { 0x98, 0xc9, 0x6f, 0xf2, 0x6a, 0xaf, 0xa6, 0x00 } };
static const GUID KSPROPSETID_MB86M01_StreamFixedSize = 
    { 0x59ef01cf, 0xe393, 0x4e97, { 0x89, 0x0c, 0x4a, 0xda, 0x93, 0xb9, 0x5f, 0x91 } };
static const GUID KSPROPSETID_MB86M01_PID_CMD_REAL = 
    { 0x048dc5d8, 0xd1cf, 0x4140, { 0x8b, 0xc5, 0x3a, 0xa1, 0x73, 0x11, 0x73, 0xa1 } };

#pragma pack(push, 1)
struct MB86M01_PID_ENTRY {
    WORD  wPid;
    WORD  wReserved1;
    DWORD dwStreamIndex; // 5 = Stream Type 5
    WORD  wParam;
    WORD  wReserved2;
};
struct MB86M01_PID_TABLE {
    BYTE  bCount;
    BYTE  bReserved[3];
    MB86M01_PID_ENTRY entries[64];
};
#pragma pack(pop)

static const GUID KSPROPSETID_GVMVPAZ_InfoCtrl =
	{ 0xa2d14551, 0x7b55, 0x46f0, { 0xa2, 0x31, 0xf1, 0xc2, 0x14, 0x8a, 0xe8, 0x57 } };
static const GUID KSPROPSETID_GVMVPAZ_RegCtrl =
	{ 0x0d31f52e, 0x4005, 0x41bf, { 0xa3, 0x8e, 0xb0, 0x85, 0x01, 0x67, 0x77, 0xc2 } };
static const GUID KSMETHODSETID_GVMVPAZ_PreCfg =
	{ 0x020bc4fe, 0x596d, 0x4520, { 0xa1, 0x70, 0xd3, 0xcd, 0x6e, 0xb6, 0xcd, 0x85 } };
static const GUID KSPROPSETID_GVMVPAZ_Status =
	{ 0x3d1be1c6, 0x9240, 0x4f71, { 0x9c, 0xed, 0xa4, 0x12, 0x8b, 0x60, 0x47, 0x27 } };
static const GUID KSMETHODSETID_GVMVPAZ_Auth =
	{ 0x5250e9e6, 0xba8c, 0x455f, { 0x9c, 0xf3, 0x6b, 0x09, 0xa1, 0x35, 0x1c, 0x80 } };

static BOOL PerformGvmvpazAuth(IKsControl *pKsControl, bool quiet = false)
{
	if (!pKsControl) return FALSE;

	#pragma pack(push, 1)
	struct AuthCmd40 { KSMETHOD m; DWORD extra[4]; };
	struct AuthSet56 { KSMETHOD m; DWORD zero[3]; DWORD r[5]; };
	#pragma pack(pop)

	auto queryStatus = [&](const wchar_t *phase) {
		AuthCmd40 cmd = { { KSMETHODSETID_GVMVPAZ_Auth, 2, 0 }, { 0 } };
		DWORD out[2] = { 0, 0 };
		ULONG ret = 0;
		HRESULT hr = pKsControl->KsMethod((PKSMETHOD)&cmd, sizeof(cmd), out, sizeof(out), &ret);
		if (!quiet) {
			OutputDebug(L"  [PreRun] 5250E9E6 id=2 (%s): hr=0x%08X ret=%lu status=%lu res=%lu\n",
				phase, hr, ret, out[0], out[1]);
		}
	};

	if (!quiet) {
		queryStatus(L"pre");
	}

	AuthCmd40 m0 = { { KSMETHODSETID_GVMVPAZ_Auth, 0, 0 }, { 0 } };
	DWORD chal[6] = { 0 };
	ULONG m0Ret = 0;
	HRESULT hrM0 = pKsControl->KsMethod((PKSMETHOD)&m0, sizeof(m0), chal, sizeof(chal), &m0Ret);
	if (!quiet || FAILED(hrM0)) {
		OutputDebug(L"  [PreRun] 5250E9E6 id=0 (seed): hr=0x%08X ret=%lu flag=%lu chal=%08X %08X %08X %08X %08X\n",
			hrM0, m0Ret, chal[5], chal[0], chal[1], chal[2], chal[3], chal[4]);
	}
	if (FAILED(hrM0)) return FALSE;

	DWORD d0 = chal[0], d1 = chal[1], d2 = chal[2], d3 = chal[3], d4 = chal[4];
	AuthSet56 m1 = { { KSMETHODSETID_GVMVPAZ_Auth, 1, 0 }, { 0, 0, 0 }, {
		((d1 ^ d3) & 0x5AA55AA5UL) ^ d3,
		(d4 - 0x37UL) ^ (d1 - 0x37UL),
		(d0 - 3UL) ^ (d2 - 5UL) ^ d4 ^ 0x395A7C2DUL,
		(d0 + d1 + d2 + d3 + d4) ^ d0 ^ d4 ^ d2 ^ d1 ^ d3,
		(d0 - 1UL) ^ (d2 + 1UL) ^ d4
	} };
	BYTE out1 = 0xFF;
	ULONG m1Ret = 0;
	HRESULT hrM1 = pKsControl->KsMethod((PKSMETHOD)&m1, sizeof(m1), &out1, sizeof(out1), &m1Ret);
	if (!quiet || FAILED(hrM1)) {
		OutputDebug(L"  [PreRun] 5250E9E6 id=1 (resp): hr=0x%08X ret=%lu out=0x%02X\n", hrM1, m1Ret, out1);
	}

	if (!quiet) {
		queryStatus(L"post");
	}
	return SUCCEEDED(hrM1);
}

BOOL CBonTuner::RefreshGvmvpazAuth(void)
{
	if (!m_pTunerDevice) {
		return FALSE;
	}
	CComPtr<IKsControl> pKsControl;
	if (FAILED(m_pTunerDevice->QueryInterface(IID_IKsControl_Local, (void**)&pKsControl)) || !pKsControl) {
		return FALSE;
	}
	return PerformGvmvpazAuth(pKsControl, true);
}

static HRESULT SetExactProp(IKsPropertySet *pPropSet, REFGUID guidSet, DWORD dwId, const BYTE *pBytes, DWORD cbSize, const wchar_t *label)
{
	BYTE buf[1024];
	if (cbSize > sizeof(buf)) return E_INVALIDARG;
	memcpy(buf, pBytes, cbSize);
	HRESULT hr = pPropSet->Set(guidSet, dwId, buf, cbSize, buf, cbSize);
	OutputDebug(L"  [PreRun] %s id=%lu (%luB): hr=0x%08X\n", label, dwId, cbSize, hr);
	return hr;
}

HRESULT CBonTuner::EnableHardwareThrough(IBaseFilter *pFilter)
{
	if (!pFilter) return E_POINTER;
	OutputDebug(L"=== [EnableHardwareThrough] Runtime-captured mAgicTV pre-Run sequence ===\n");

	CComQIPtr<IKsPropertySet> pPropSet(pFilter);
	CComPtr<IKsControl> pKsControl;
	pFilter->QueryInterface(IID_IKsControl_Local, (void**)&pKsControl);

	if (pPropSet) {
		BYTE infoBuf[108] = { 0 };
		DWORD dwInfoRet = 0;
		HRESULT hrInfo = pPropSet->Get(KSPROPSETID_GVMVPAZ_InfoCtrl, 3, infoBuf, sizeof(infoBuf), infoBuf, sizeof(infoBuf), &dwInfoRet);
		OutputDebug(L"  [PreRun] A2D14551 id=3 GET: hr=0x%08X ret=%lu name=\"%.28S\"\n", hrInfo, dwInfoRet, (const char*)(infoBuf + 32));
	}

	if (pKsControl) {
		KSMETHOD m1 = { KSMETHODSETID_GVMVPAZ_Through, 1, 0 };
		BYTE m1Out[20] = { 0 };
		ULONG m1Ret = 0;
		HRESULT hrM1 = pKsControl->KsMethod(&m1, sizeof(m1), m1Out, sizeof(m1Out), &m1Ret);
		OutputDebug(L"  [PreRun] 74395BA9 id=1: hr=0x%08X ret=%lu\n", hrM1, m1Ret);
	}

	if (pPropSet) {
		BYTE r8[8] = { 0x08, 0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 };
		DWORD dwR = 0;
		HRESULT hrR8 = pPropSet->Get(KSPROPSETID_GVMVPAZ_RegCtrl, 7, r8, 8, r8, 8, &dwR);
		OutputDebug(L"  [PreRun] 0D31F52E id=7 GET(82008): hr=0x%08X val=0x%08X\n", hrR8, *(DWORD*)(r8 + 4));

		BYTE r4[8] = { 0x04, 0x20, 0x08, 0x00, 0x00, 0x00, 0xFC, 0x43 };
		HRESULT hrR4 = pPropSet->Get(KSPROPSETID_GVMVPAZ_RegCtrl, 7, r4, 8, r4, 8, &dwR);
		OutputDebug(L"  [PreRun] 0D31F52E id=7 GET(82004): hr=0x%08X val=0x%08X\n", hrR4, *(DWORD*)(r4 + 4));

		BYTE r4Set[8] = { 0x04, 0x20, 0x08, 0x00, 0x10, 0x00, 0xFC, 0x43 };
		SetExactProp(pPropSet, KSPROPSETID_GVMVPAZ_RegCtrl, 7, r4Set, 8, L"0D31F52E");
	}

	if (pKsControl) {
		PerformGvmvpazAuth(pKsControl);

		#pragma pack(push, 1)
		struct { KSMETHOD m; BYTE extra[16]; } m24 = { { KSMETHODSETID_GVMVPAZ_Through, 24, 0 }, { 1 } };
		struct { KSMETHOD m; BYTE extra[8]; } m26 = { { KSMETHODSETID_GVMVPAZ_Through, 26, 0 }, { 1 } };
		#pragma pack(pop)
		BYTE m24Out[20] = { 0 };
		ULONG m24Ret = 0;
		HRESULT hrM24 = pKsControl->KsMethod((PKSMETHOD)&m24, sizeof(m24), m24Out, sizeof(m24Out), &m24Ret);
		DWORD m24Status = *(DWORD*)m24Out;
		OutputDebug(L"  [PreRun] 74395BA9 id=24: hr=0x%08X ret=%lu status=0x%08X\n", hrM24, m24Ret, m24Status);
		if (SUCCEEDED(hrM24) && m24Status == 0x00020019) {
			DWORD m26Out = 0;
			ULONG m26Ret = 0;
			HRESULT hrM26 = pKsControl->KsMethod((PKSMETHOD)&m26, sizeof(m26), &m26Out, sizeof(m26Out), &m26Ret);
			OutputDebug(L"  [PreRun] 74395BA9 id=26 (reset): hr=0x%08X ret=%lu out=0x%08X\n", hrM26, m26Ret, m26Out);
			ZeroMemory(m24Out, sizeof(m24Out));
			m24Ret = 0;
			hrM24 = pKsControl->KsMethod((PKSMETHOD)&m24, sizeof(m24), m24Out, sizeof(m24Out), &m24Ret);
			m24Status = *(DWORD*)m24Out;
			OutputDebug(L"  [PreRun] 74395BA9 id=24 retry: hr=0x%08X ret=%lu status=0x%08X\n", hrM24, m24Ret, m24Status);
		}
		if (SUCCEEDED(hrM24) && m24Status == 0 && m24Ret >= 20) {
			SetGvmvpazSessionKey(m24Out + 4);
		}

		KSMETHOD mPre = { KSMETHODSETID_GVMVPAZ_PreCfg, 0, 0 };
		DWORD preOut = 0;
		ULONG preRet = 0;
		HRESULT hrPre = pKsControl->KsMethod(&mPre, sizeof(mPre), &preOut, sizeof(preOut), &preRet);
		OutputDebug(L"  [PreRun] 020BC4FE id=0: hr=0x%08X ret=%lu out=0x%08X\n", hrPre, preRet, preOut);
	}

	if (pPropSet) {
		static const BYTE cd0[6]  = { 0x00,0x00,0x00,0x00,0x01,0x00 };
		static const BYTE cd1[8]  = { 0x00,0x00,0x00,0x00,0x0B,0x00,0x00,0x00 };
		static const BYTE cd2[14] = { 0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x0F,0x00,0x00,0x00 };
		static const BYTE cd3[6]  = { 0x03,0x00,0x01,0x00,0x01,0x00 };
		static const BYTE cd4[42] = { 0x01,0x00,0x00,0x00, 0x90,0x65,0x00,0x00, 0x02,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
		                              0x00,0x00,0x00,0x00, 0x04,0x00,0x01,0x00, 0x00,0x00,0x02,0x00, 0x0F,0x00,0x02,0x00,
		                              0x00,0x00,0xE0,0x00, 0x01,0x00,0x00,0x00, 0xC0,0x00 };
		static const BYTE cd5[44] = { 0x01,0x00,0x00,0x00, 0x90,0x65,0x00,0x00, 0x02,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
		                              0x00,0x00,0x00,0x00, 0x04,0x00,0x01,0x00 };
		BYTE cd6[246] = { 0 };
		cd6[70] = 0x01; cd6[90] = 0x01; cd6[92] = 0x01;
		BYTE cd7[94] = { 0 };
		cd7[0] = 0x01; cd7[58] = 0x01;

		SetExactProp(pPropSet, KSPROPSETID_MB86M01_ForcePmtThrough, 0, cd0, sizeof(cd0), L"CD0B5B68");
		SetExactProp(pPropSet, KSPROPSETID_MB86M01_ForcePmtThrough, 1, cd1, sizeof(cd1), L"CD0B5B68");
		SetExactProp(pPropSet, KSPROPSETID_MB86M01_ForcePmtThrough, 2, cd2, sizeof(cd2), L"CD0B5B68");
		SetExactProp(pPropSet, KSPROPSETID_MB86M01_ForcePmtThrough, 3, cd3, sizeof(cd3), L"CD0B5B68");
		SetExactProp(pPropSet, KSPROPSETID_MB86M01_ForcePmtThrough, 4, cd4, sizeof(cd4), L"CD0B5B68");
		SetExactProp(pPropSet, KSPROPSETID_MB86M01_ForcePmtThrough, 5, cd5, sizeof(cd5), L"CD0B5B68");
		SetExactProp(pPropSet, KSPROPSETID_MB86M01_ForcePmtThrough, 6, cd6, sizeof(cd6), L"CD0B5B68");
		SetExactProp(pPropSet, KSPROPSETID_MB86M01_ForcePmtThrough, 7, cd7, sizeof(cd7), L"CD0B5B68");

		MB86M01_PID_TABLE pidTable;
		ZeroMemory(&pidTable, sizeof(pidTable));
		static const WORD pidsToRoute[] = { 0x0010, 0x0011, 0x0012 };
		DWORD numPids = sizeof(pidsToRoute) / sizeof(pidsToRoute[0]);
		pidTable.bCount = (BYTE)numPids;
		for (DWORD i = 0; i < numPids; i++) {
			pidTable.entries[i].wPid = pidsToRoute[i];
			pidTable.entries[i].dwStreamIndex = 5;
		}
		HRESULT hrPid = pPropSet->Set(KSPROPSETID_MB86M01_PID_CMD_REAL, 0, &pidTable, sizeof(pidTable), &pidTable, sizeof(pidTable));
		OutputDebug(L"  [PreRun] 048DC5D8 PID table (%lu, PSI idx=5 only): hr=0x%08X\n", numPids, hrPid);

		DWORD s1 = 0, dwRet = 0;
		HRESULT hrS1 = pPropSet->Get(KSPROPSETID_GVMVPAZ_Status, 1, &s1, sizeof(s1), &s1, sizeof(s1), &dwRet);
		BYTE s0[200] = { 0 };
		HRESULT hrS0 = pPropSet->Get(KSPROPSETID_GVMVPAZ_Status, 0, s0, sizeof(s0), s0, sizeof(s0), &dwRet);
		OutputDebug(L"  [PreRun] 3D1BE1C6 id=1 hr=0x%08X val=%lu, id=0 hr=0x%08X d0=%lu\n", hrS1, s1, hrS0, *(DWORD*)s0);
	}

	return S_OK;
}

struct GvmvpazPidSpec { WORD pid; DWORD streamIdx; WORD param; };

static void AppendUniquePid(GvmvpazPidSpec *arr, DWORD &count, WORD pid, DWORD streamIdx, WORD param)
{
	if (pid == 0xFFFF || pid == 0x1FFF) return;
	for (DWORD i = 0; i < count; i++) {
		if (arr[i].pid == pid) return;
	}
	if (count < 64) {
		arr[count].pid = pid;
		arr[count].streamIdx = streamIdx;
		arr[count].param = param;
		count++;
	}
}

static void CollectCaPids(const BYTE *desc, size_t descLen, GvmvpazPidSpec *arr, DWORD &count)
{
	size_t pos = 0;
	while (pos + 2 <= descLen) {
		BYTE tag = desc[pos];
		BYTE dlen = desc[pos + 1];
		if (pos + 2 + dlen > descLen) break;
		if (tag == 0x09 && dlen >= 4) {
			WORD caPid = ((WORD)(desc[pos + 4] & 0x1F) << 8) | desc[pos + 5];
			AppendUniquePid(arr, count, caPid, 5, 0);
		}
		pos += 2 + dlen;
	}
}

static HRESULT SendGvmvpazPidTable(IKsPropertySet *pPropSet, const GvmvpazPidSpec *specs, DWORD count, const wchar_t *label)
{
	MB86M01_PID_TABLE tbl = {};
	DWORD clamped = (count > 64) ? 64 : count;
	tbl.bCount = (BYTE)clamped;
	for (DWORD i = 0; i < clamped; i++) {
		tbl.entries[i].wPid = specs[i].pid;
		tbl.entries[i].dwStreamIndex = specs[i].streamIdx;
		tbl.entries[i].wParam = specs[i].param;
	}
	HRESULT hr = pPropSet->Set(KSPROPSETID_MB86M01_PID_CMD_REAL, 0, &tbl, sizeof(tbl), &tbl, sizeof(tbl));
	OutputDebug(L"  [PostTune] 048DC5D8 (%s, cnt=%lu): hr=0x%08X\n", label, clamped, hr);
	return hr;
}

static bool WaitAndCopyPsiSection(PsiSectionAssembler &a, BYTE *outBuf, size_t &outLen, DWORD timeoutMs)
{
	DWORD waited = 0;
	while (waited < timeoutMs && InterlockedCompareExchange(&a.ready, 1, 1) != 1) {
		Sleep(20);
		waited += 20;
	}
	bool ok = false;
	EnsureCs(g_psiCs, g_psiCsInit);
	EnterCriticalSection(&g_psiCs);
	if (a.ready == 1 && a.expected >= 12 && a.expected <= 1024) {
		memcpy(outBuf, a.sec, a.expected);
		outLen = a.expected;
		ok = true;
	}
	InterlockedExchange(&a.ready, 2);
	LeaveCriticalSection(&g_psiCs);
	return ok;
}

static void PostTuneGvmvpazEnable(IBaseFilter *pFilter, DWORD requestedSid)
{
	if (!pFilter) return;
	static const GUID KSPROPSETID_BdaTableSection_Local =
		{ 0x516b99c5, 0x971c, 0x4aaf, { 0xb3, 0xf3, 0xd9, 0xfd, 0xa8, 0xa1, 0x5e, 0x16 } };
	static const WORD basePsiPids[] = { 0x0000, 0x0010, 0x0011, 0x0012, 0x0014, 0x0024, 0x1FC8 };

	CComQIPtr<IKsPropertySet> pPropSet(pFilter);
	if (!pPropSet) return;

	// Step 1: Request PAT + base PSI/SI (idx=5) and wait for PAT section
	ResetPsiAssembler(g_pmtAsm, 0xFFFF, 0x02, 0xFFFF, 2);
	ResetPsiAssembler(g_patAsm, 0x0000, 0x00, 0xFFFF, 0);

	GvmvpazPidSpec patReqPids[64] = {};
	DWORD patReqCount = 0;
	for (size_t i = 0; i < sizeof(basePsiPids) / sizeof(basePsiPids[0]); i++) {
		AppendUniquePid(patReqPids, patReqCount, basePsiPids[i], 5, 0);
	}
	SendGvmvpazPidTable(pPropSet, patReqPids, patReqCount, L"PAT idx=5");

	BYTE patSec[1024] = {};
	size_t patLen = 0;
	if (!WaitAndCopyPsiSection(g_patAsm, patSec, patLen, 2000) || patLen < 16) {
		OutputDebug(L"  [PostTune] PAT 0x0000 not received within 2000ms\n");
		return;
	}

	// Step 2: Parse PAT for target (service_id, pmt_pid) and additional service PMTs
	WORD serviceId = 0, pmtPid = 0xFFFF;
	WORD firstFullSid = 0, firstFullPid = 0xFFFF;
	WORD firstAnySid = 0, firstAnyPid = 0xFFFF;
	WORD allPmtPids[16] = {};
	DWORD allPmtCount = 0;
	bool hasReqSid = (requestedSid != 0 && requestedSid != 0xFFFFFFFF && requestedSid != 0xFFFF);

	for (size_t pos = 8; pos + 4 <= patLen - 4; pos += 4) {
		WORD prog = ((WORD)patSec[pos] << 8) | patSec[pos + 1];
		WORD pid = ((WORD)(patSec[pos + 2] & 0x1F) << 8) | patSec[pos + 3];
		if (prog == 0 || pid == 0x1FFF) continue;
		if (allPmtCount < 16) allPmtPids[allPmtCount++] = pid;
		if (hasReqSid && prog == (WORD)requestedSid && pmtPid == 0xFFFF) {
			serviceId = prog;
			pmtPid = pid;
		}
		if (pid < 0x1FC0 && firstFullPid == 0xFFFF) {
			firstFullSid = prog;
			firstFullPid = pid;
		}
		if (firstAnyPid == 0xFFFF) {
			firstAnySid = prog;
			firstAnyPid = pid;
		}
	}
	if (pmtPid == 0xFFFF) {
		if (firstFullPid != 0xFFFF) {
			serviceId = firstFullSid;
			pmtPid = firstFullPid;
		} else if (firstAnyPid != 0xFFFF) {
			serviceId = firstAnySid;
			pmtPid = firstAnyPid;
		}
	}
	if (pmtPid == 0xFFFF) {
		OutputDebug(L"  [PostTune] PAT contained no valid service PMT PID (patLen=%Iu)\n", patLen);
		return;
	}
	OutputDebug(L"  [PostTune] PAT resolved sid=0x%04X pmtPid=0x%04X (reqSid=0x%08X)\n", serviceId, pmtPid, requestedSid);

	// Step 3: Request PMT via 048DC5D8 (pmtPid idx=2 wParam=serviceId + PSI/SI idx=5; NO idx=0/1/3 yet!)
	ResetPsiAssembler(g_pmtAsm, pmtPid, 0x02, serviceId, 0);
	GvmvpazPidSpec prePmtPids[64] = {};
	DWORD prePmtCount = 0;
	AppendUniquePid(prePmtPids, prePmtCount, pmtPid, 2, serviceId);
	for (size_t i = 0; i < sizeof(basePsiPids) / sizeof(basePsiPids[0]); i++) {
		AppendUniquePid(prePmtPids, prePmtCount, basePsiPids[i], 5, 0);
	}
	for (DWORD i = 0; i < allPmtCount; i++) {
		AppendUniquePid(prePmtPids, prePmtCount, allPmtPids[i], 5, 0);
	}
	SendGvmvpazPidTable(pPropSet, prePmtPids, prePmtCount, L"pre-PMT idx=2");

	BYTE pmtSec[1024] = {};
	size_t pmtLen = 0;
	if (!WaitAndCopyPsiSection(g_pmtAsm, pmtSec, pmtLen, 2000) || pmtLen < 16) {
		OutputDebug(L"  [PostTune] PMT 0x%04X (sid=0x%04X) not received within 2000ms\n", pmtPid, serviceId);
		return;
	}

	// Step 4: Parse PMT for PCR, Video, Audio, CA ECM, and supplementary ES PIDs
	WORD pcrPid = ((WORD)(pmtSec[8] & 0x1F) << 8) | pmtSec[9];
	size_t progInfoLen = ((size_t)(pmtSec[10] & 0x0F) << 8) | pmtSec[11];
	size_t sectionEnd = pmtLen - 4;
	if (12 + progInfoLen > sectionEnd) progInfoLen = (sectionEnd > 12) ? (sectionEnd - 12) : 0;

	GvmvpazPidSpec extraPids[64] = {};
	DWORD extraCount = 0;
	CollectCaPids(pmtSec + 12, progInfoLen, extraPids, extraCount);

	WORD videoPid = 0xFFFF, audioPid = 0xFFFF;
	size_t pos = 12 + progInfoLen;
	while (pos + 5 <= sectionEnd) {
		BYTE streamType = pmtSec[pos];
		WORD esPid = ((WORD)(pmtSec[pos + 1] & 0x1F) << 8) | pmtSec[pos + 2];
		size_t esInfoLen = ((size_t)(pmtSec[pos + 3] & 0x0F) << 8) | pmtSec[pos + 4];
		if (pos + 5 + esInfoLen > sectionEnd) esInfoLen = sectionEnd - (pos + 5);

		if ((streamType == 0x02 || streamType == 0x1B) && videoPid == 0xFFFF) {
			videoPid = esPid;
		} else if ((streamType == 0x0F || streamType == 0x03 || streamType == 0x04) && audioPid == 0xFFFF) {
			audioPid = esPid;
		} else {
			AppendUniquePid(extraPids, extraCount, esPid, 5, 0);
		}
		CollectCaPids(pmtSec + pos + 5, esInfoLen, extraPids, extraCount);
		pos += 5 + esInfoLen;
	}
	OutputDebug(L"  [PostTune] PMT parsed: pcr=0x%04X vid=0x%04X aud=0x%04X extra=%lu len=%Iu\n",
		pcrPid, videoPid, audioPid, extraCount, pmtLen);

	GvmvpazPidSpec postPids[64] = {};
	DWORD postCount = 0;
	if (pcrPid != 0 && pcrPid != 0x1FFF && pcrPid != videoPid && pcrPid != audioPid) {
		AppendUniquePid(postPids, postCount, pcrPid, 3, 0);
	}
	AppendUniquePid(postPids, postCount, videoPid, 0, 0);
	AppendUniquePid(postPids, postCount, audioPid, 1, 0);
	for (DWORD i = 0; i < extraCount; i++) {
		AppendUniquePid(postPids, postCount, extraPids[i].pid, 5, 0);
	}
	for (size_t i = 0; i < sizeof(basePsiPids) / sizeof(basePsiPids[0]); i++) {
		AppendUniquePid(postPids, postCount, basePsiPids[i], 5, 0);
	}
	for (DWORD i = 0; i < allPmtCount; i++) {
		if (allPmtPids[i] != pmtPid) {
			AppendUniquePid(postPids, postCount, allPmtPids[i], 5, 0);
		}
	}

	// Step 5: Send PMT via BdaTableSection on "Stream Out" pin, THEN program ES/PCR PID table
	CComPtr<IPin> pStreamOut;
	CDSEnumPins enumPins(pFilter);
	CComPtr<IPin> pPin;
	while (S_OK == enumPins.getNextPin(&pPin, PIN_DIRECTION::PINDIR_OUTPUT)) {
		PIN_INFO pi = {};
		if (SUCCEEDED(pPin->QueryPinInfo(&pi))) {
			if (pi.pFilter) pi.pFilter->Release();
			if (wcscmp(pi.achName, L"Stream Out") == 0) {
				pStreamOut = pPin;
				pPin.Release();
				break;
			}
		}
		pPin.Release();
	}
	CComQIPtr<IKsPropertySet> pPinProp(pStreamOut);
	if (pPinProp) {
		BYTE secBuf[1040] = {};
		DWORD secFieldLen = (DWORD)(pmtLen - 3);
		*(DWORD*)(secBuf + 0) = 2; // ulPrimarySectionId
		*(DWORD*)(secBuf + 4) = 0; // ulSecondarySectionId
		*(DWORD*)(secBuf + 8) = secFieldLen; // ulcbSectionLength
		memcpy(secBuf + 12, pmtSec, pmtLen);
		// Swap Header (bytes 1..2) and TableIdExtension (bytes 3..4) to match mpeg2structs LONG_SECTION
		BYTE tmp = secBuf[12 + 1]; secBuf[12 + 1] = secBuf[12 + 2]; secBuf[12 + 2] = tmp;
		tmp = secBuf[12 + 3]; secBuf[12 + 3] = secBuf[12 + 4]; secBuf[12 + 4] = tmp;
		DWORD totalBuf = (DWORD)(12 + pmtLen);
		DWORD inst[8] = {}; // 32B instance data (KSP_NODE+)
		HRESULT hrSec = pPinProp->Set(KSPROPSETID_BdaTableSection_Local, 0, inst, sizeof(inst), secBuf, totalBuf);
		OutputDebug(L"  [PostTune] BdaTableSection (secLen=%lu total=%lu): hr=0x%08X\n", secFieldLen, totalBuf, hrSec);

		SendGvmvpazPidTable(pPropSet, postPids, postCount, L"post-PMT");
	} else {
		OutputDebug(L"  [PostTune] Stream Out pin IKsPropertySet not found!\n");
	}
}

HRESULT CBonTuner::RunGraph(void)
{
	if (!m_pIMediaControl)
		return E_POINTER;

	OAFilterState fs = State_Stopped;
	if (SUCCEEDED(m_pIMediaControl->GetState(0, &fs)) && fs == State_Running) {
		OutputDebug(L"  [RunGraph] Graph already State_Running.\n");
		return S_OK;
	}

	SAFE_CLOSE_HANDLE(m_hStreamThread);
	m_bIsSetStreamThread = FALSE;

	// 1. Apply runtime-captured pre-Run tuner configuration BEFORE Pause/Run
	if (m_pTunerDevice) { EnableHardwareThrough(m_pTunerDevice); }

	// 2. Keep default/KS reference clock (matching mAgicTV KSClock usage)

	// 3. Step 1: Transition Graph to Pause via Filter Graph Manager
	// (Graph Manager coordinates pins: Allocates KS buffers, transitions STOP -> ACQUIRE -> PAUSE)
	OutputDebug(L"--- RunGraph: Transition Graph to Pause (m_pIMediaControl->Pause()) ---\n");
	HRESULT hrP = m_pIMediaControl->Pause();
	OutputDebug(L"  [RunGraph] m_pIMediaControl->Pause() returned 0x%08X\n", hrP);

	// 4. Step 2: Transition Graph to Run via Filter Graph Manager
	OutputDebug(L"--- RunGraph: Transition Graph to Run (m_pIMediaControl->Run()) ---\n");
	HRESULT hrR = m_pIMediaControl->Run();
	OutputDebug(L"  [RunGraph] m_pIMediaControl->Run() returned 0x%08X\n", hrR);

	return FAILED(hrR) ? hrR : S_OK;
}

void CBonTuner::StopGraph(void)
{
	if (m_pIMediaControl) {
		SAFE_CLOSE_HANDLE(m_hStreamThread);
		m_bIsSetStreamThread = FALSE;

		HRESULT hr = m_pIMediaControl->Stop();
		OutputDebug(L"  [StopGraph] m_pIMediaControl->Stop() returned 0x%08X\n", hr);
	}
	if (m_pTunerDevice) {
		CComPtr<IKsControl> pKsControl;
		if (SUCCEEDED(m_pTunerDevice->QueryInterface(IID_IKsControl_Local, (void**)&pKsControl)) && pKsControl) {
			#pragma pack(push, 1)
			struct { KSMETHOD m; BYTE extra[8]; } m26 = { { KSMETHODSETID_GVMVPAZ_Through, 26, 0 }, { 1 } };
			#pragma pack(pop)
			DWORD m26Out = 0;
			ULONG m26Ret = 0;
			HRESULT hrM26 = pKsControl->KsMethod((PKSMETHOD)&m26, sizeof(m26), &m26Out, sizeof(m26Out), &m26Ret);
			OutputDebug(L"  [StopGraph] 74395BA9 id=26 release: hr=0x%08X ret=%lu out=0x%08X\n", hrM26, m26Ret, m26Out);
		}
	}
}

HRESULT CBonTuner::CreateTuningSpace(void)
{
	for (auto it = m_DVBSystemTypeDB.SystemType.begin(); it != m_DVBSystemTypeDB.SystemType.end(); it++) {
		OutputDebug(L"[CreateTuningSpace] Processing number %ld.\n", it->first);

		// IuWFNgp
		enumTuningSpace specifyTuningSpace = eTuningSpaceAuto;					// gpTuningSpaceIuWFNg
		CLSID clsidTuningSpace = CLSID_NULL;									// TuningSpaceIuWFNgﾌクXid
		enumLocator specifyLocator = eLocatorAuto;								// gpLocatorIuWFNg
		CLSID clsidLocator = CLSID_NULL;										// LocatorIuWFNgﾌクXid

		// TuningSpaceﾝ恥
		// ITuningSpace
		_bstr_t bstrUniqueName;													// ITuningSpaceﾉ設定すUniqueName
		_bstr_t bstrFriendlyName;												// ITuningSpaceﾉ設定すFriendlyName
		enumNetworkType specifyITuningSpaceNetworkType = eNetworkTypeAuto;		// ITuningSpaceﾉ設定すNetworkType
		IID iidNetworkType = IID_NULL;											// ITuningSpaceﾉ設定すNetworkTypeGUID
		// IDVBTuningSpace
		DVBSystemType dvbSystemType = DVB_Satellite;							// DVBﾌシXe^Cv
		// IDVBTuningSpace2
		long networkID = -1;													// Network ID
		// IDVBSTuningSpace
		long highOscillator = -1;												// HighOscillatorg
		long lowOscillator = -1;												// LowOscillatorg
		long lnbSwitch = -1;													// LNBXCb`g
		SpectralInversion spectralInversion = BDA_SPECTRAL_INVERSION_NOT_SET;	// XyNg]
		// IAnalogTVTuningSpace
		TunerInputType inputType = TunerInputCable;								// AeiEP[uﾌ届タCv
		long countryCode = 0;													// EnR[h
		long minChannel = 0;													// Channelﾔ採最粛
		long maxChannel = 0;													// Channelﾔ採最鼠
		// IATSCTuningSpace
		long minPhysicalChannel = 0;											// Physical Channelﾔ採最粛
		long maxPhysicalChannel = 0;											// Physical Channelﾔ採最鼠
		long minMinorChannel = 0;												// Minor Channelﾔ採最粛
		long maxMinorChannel = 0;												// Minor Channelﾔ採最鼠
		// IDigitalCableTuningSpace
		long minMajorChannel = 0;												// Major Channelﾔ採最粛
		long maxMajorChannel = 0;												// Major Channelﾔ採最鼠
		long minSourceID = 0;													// Source IDﾌ最粛
		long maxSourceID = 0;													// Source IDﾌ最鼠

		// Default Locatorﾝ恥
		// ILocator
		long frequency = -1;													// RFMﾌ使
		long symbolRate = -1;													// V{[g
		FECMethod innerFECMethod = BDA_FEC_METHOD_NOT_SET;						// O^Cv
		BinaryConvolutionCodeRate innerFECRate = BDA_BCC_RATE_NOT_SET;			// FEC[g
		FECMethod outerFECMethod = BDA_FEC_METHOD_NOT_SET;						// OO^Cv
		BinaryConvolutionCodeRate outerFECRate = BDA_BCC_RATE_NOT_SET;			// OFEC[g
		ModulationType modulationType = BDA_MOD_NOT_SET;						// ﾏ耽Cv
		// IDVBSLocator
		VARIANT_BOOL westPosition = VARIANT_TRUE;								// qﾌ経x�ｼ経ﾆ登ﾌどで表驍ｩ(Trueﾅ塵)
		long orbitalPosition = -1;												// qﾌ経x(1/10)
		long elevation = -1;													// qﾌ仰角(1/10)
		long azimuth = -1;														// qﾌ別角(1/10)
		Polarisation polarisation = BDA_POLARISATION_NOT_SET;					// ﾎ波l
		// IDVBSLocator2
		LNB_Source diseqLNBSource = BDA_LNB_SOURCE_NOT_SET;						// DiSeqC LNBﾍソ[X
		Pilot pilot = BDA_PILOT_NOT_SET;										// DVB-S2pCbg[h
		RollOff rollOff = BDA_ROLL_OFF_NOT_SET;									// DVB-S2[ItW
		// IDVBTLocator
		long bandwidth = -1;													// ﾑ域幅(MHz)
		GuardInterval guardInterval = BDA_GUARD_NOT_SET;						// K[hC^[o
		HierarchyAlpha hierarchyAlpha = BDA_HALPHA_NOT_SET;						// KwAt@
		FECMethod lpInnerFECMethod = BDA_FEC_METHOD_NOT_SET;					// LPXg[ﾌ徹^Cv
		BinaryConvolutionCodeRate lpInnerFECRate = BDA_BCC_RATE_NOT_SET;		// LPXg[ﾌ擢EC[g
		TransmissionMode transmissionMode = BDA_XMIT_MODE_NOT_SET;				// `[h
		VARIANT_BOOL otherFrequencyInUse = VARIANT_TRUE;						// DVB-Tu[hLX^ﾅ使ﾄるか
		// IDVBTLocator2
		long physicalLayerPipeId = -1;											// PLP ID
		// IATSCLocator
		long physicalChannel = -1;												// Physical Channel
		long transportStreamID = -1;											// TSID
		// IATSCLocator2
		long programNumber = -1;												// Program Number

		switch (it->second.nDVBSystemType) {
		case eTunerTypeDVBT:
		case eTunerTypeDVBT2:
			bstrUniqueName = L"DVB-T";
			bstrFriendlyName = L"Local DVB-T Digital Antenna";
			specifyTuningSpace = eTuningSpaceDVB;
			if (it->second.nDVBSystemType == eTunerTypeDVBT2) {
				specifyLocator = eLocatorDVBT2;
			}
			else {
				specifyLocator = eLocatorDVBT;
			}
			specifyITuningSpaceNetworkType = eNetworkTypeDVBT;
			dvbSystemType = DVB_Terrestrial;
			networkID = 0;
			break;

		case eTunerTypeDVBC:
			bstrUniqueName = L"DVB-C";
			bstrFriendlyName = L"Local DVB-C Digital Cable";
			specifyTuningSpace = eTuningSpaceDVB;
			specifyLocator = eLocatorDVBC;
			specifyITuningSpaceNetworkType = eNetworkTypeDVBC;
			dvbSystemType = DVB_Cable;
			networkID = 0;
			break;

		case eTunerTypeISDBT:
			bstrUniqueName = L"ISDB-T";
			bstrFriendlyName = L"Local ISDB-T Digital Antenna";
			specifyTuningSpace = eTuningSpaceDVB;
			specifyLocator = eLocatorDVBT;
			specifyITuningSpaceNetworkType = eNetworkTypeISDBT;
			dvbSystemType = ISDB_Terrestrial;
			networkID = -1;
			break;

		case eTunerTypeISDBC:
			bstrUniqueName = L"ISDB-C";
			bstrFriendlyName = L"Local ISDB-C Digital Cable";
			specifyTuningSpace = eTuningSpaceDVB;
			specifyLocator = eLocatorDVBC;
			specifyITuningSpaceNetworkType = eNetworkTypeISDBC;
			dvbSystemType = DVB_Cable;
			networkID = -1;
			break;

		case eTunerTypeISDBS:
			bstrUniqueName = L"ISDB-S";
			bstrFriendlyName = L"Default Digital ISDB-S Tuning Space";
			specifyTuningSpace = eTuningSpaceDVBS;
			specifyLocator = eLocatorISDBS;
			specifyITuningSpaceNetworkType = eNetworkTypeISDBS;
			dvbSystemType = ISDB_Satellite;
			networkID = -1;
			highOscillator = -1;
			lowOscillator = -1;
			lnbSwitch = -1;
			spectralInversion = BDA_SPECTRAL_INVERSION_NOT_SET;
			break;

		case eTunerTypeATSC_Antenna:
			bstrUniqueName = L"ATSC";
			bstrFriendlyName = L"Local ATSC Digital Antenna";
			specifyTuningSpace = eTuningSpaceATSC;
			specifyLocator = eLocatorATSC;
			specifyITuningSpaceNetworkType = eNetworkTypeATSC;
			inputType = TunerInputAntenna;
			countryCode = 0;
			minChannel = 1;
			maxChannel = 99;
			minPhysicalChannel = 2;
			maxPhysicalChannel = 69;
			minMinorChannel = 0;
			maxMinorChannel = 999;
			modulationType = BDA_MOD_128QAM;
			break;

		case eTunerTypeATSC_Cable:
			bstrUniqueName = L"ATSCCable";
			bstrFriendlyName = L"Local ATSC Digital Cable";
			specifyTuningSpace = eTuningSpaceATSC;
			specifyLocator = eLocatorATSC;
			specifyITuningSpaceNetworkType = eNetworkTypeATSC;
			inputType = TunerInputCable;
			countryCode = 0;
			minChannel = 1;
			maxChannel = 99;
			minPhysicalChannel = 1;
			maxPhysicalChannel = 158;
			minMinorChannel = 0;
			maxMinorChannel = 999;
			modulationType = BDA_MOD_128QAM;
			break;

		case eTunerTypeDigitalCable:
			bstrUniqueName = L"Digital Cable";
			bstrFriendlyName = L"Local Digital Cable";
			specifyTuningSpace = eTuningSpaceDigitalCable;
			specifyLocator = eLocatorDigitalCable;
			specifyITuningSpaceNetworkType = eNetworkTypeDigitalCable;
			inputType = TunerInputCable;
			countryCode = 0;
			minChannel = 2;
			maxChannel = 9999;
			minPhysicalChannel = 2;
			maxPhysicalChannel = 158;
			minMinorChannel = 0;
			maxMinorChannel = 999;
			minMajorChannel = 1;
			maxMajorChannel = 99;
			minSourceID = 0;
			maxSourceID = 0x7fffffff;
			break;

		case eTunerTypeDVBS:
		default:
			bstrUniqueName = L"DVB-S";
			bstrFriendlyName = L"Default Digital DVB-S Tuning Space";
			specifyTuningSpace = eTuningSpaceDVBS;
			specifyLocator = eLocatorDVBS;
			specifyITuningSpaceNetworkType = eNetworkTypeDVBS;
			dvbSystemType = DVB_Satellite;
			networkID = -1;
			highOscillator = 10600000;
			lowOscillator = 9750000;
			lnbSwitch = 11700000;
			spectralInversion = BDA_SPECTRAL_INVERSION_NOT_SET;
			westPosition = VARIANT_FALSE;
			break;
		}

		if (it->second.nTuningSpace != eTuningSpaceAuto) {
			specifyTuningSpace = it->second.nTuningSpace;
		}
		switch (specifyTuningSpace) {
		case eTuningSpaceDVB:
			clsidTuningSpace = __uuidof(DVBTuningSpace);
			break;
		case eTuningSpaceDVBS:
			clsidTuningSpace = __uuidof(DVBSTuningSpace);
			break;
		case eTuningSpaceAnalogTV:
			clsidTuningSpace = __uuidof(AnalogTVTuningSpace);
			break;
		case eTuningSpaceATSC:
			clsidTuningSpace = __uuidof(ATSCTuningSpace);
			break;
		case eTuningSpaceDigitalCable:
			clsidTuningSpace = __uuidof(DigitalCableTuningSpace);
			break;
		}

		if (it->second.nLocator != eLocatorAuto) {
			specifyLocator = it->second.nLocator;
		}
		switch (specifyLocator) {
		case eLocatorDVBT:
			clsidLocator = __uuidof(DVBTLocator);
			break;
		case eLocatorDVBT2:
			clsidLocator = __uuidof(DVBTLocator2);
			break;
		case eLocatorDVBS:
			clsidLocator = __uuidof(DVBSLocator);
			break;
		case eLocatorDVBC:
			clsidLocator = __uuidof(DVBCLocator);
			break;
		case eLocatorISDBS:
			clsidLocator = __uuidof(ISDBSLocator);
			break;
		case eLocatorATSC:
			clsidLocator = __uuidof(ATSCLocator);
			break;
		case eLocatorDigitalCable:
			clsidLocator = __uuidof(DigitalCableLocator);
			break;
		}

		if (it->second.nITuningSpaceNetworkType != eNetworkTypeAuto) {
			specifyITuningSpaceNetworkType = it->second.nITuningSpaceNetworkType;
		}
		switch (specifyITuningSpaceNetworkType) {
		case eNetworkTypeDVBT:
			iidNetworkType = { STATIC_DVB_TERRESTRIAL_TV_NETWORK_TYPE };
			break;
		case eNetworkTypeDVBS:
			iidNetworkType = { STATIC_DVB_SATELLITE_TV_NETWORK_TYPE };
			break;
		case eNetworkTypeDVBC:
			iidNetworkType = { STATIC_DVB_CABLE_TV_NETWORK_TYPE };
			break;
		case eNetworkTypeISDBT:
			iidNetworkType = { STATIC_ISDB_TERRESTRIAL_TV_NETWORK_TYPE };
			break;
		case eNetworkTypeISDBS:
			iidNetworkType = { STATIC_ISDB_SATELLITE_TV_NETWORK_TYPE };
			break;
		case eNetworkTypeISDBC:
			iidNetworkType = { STATIC_ISDB_CABLE_TV_NETWORK_TYPE };
			break;
		case eNetworkTypeATSC:
			iidNetworkType = { STATIC_ATSC_TERRESTRIAL_TV_NETWORK_TYPE };
			break;
		case eNetworkTypeDigitalCable:
			iidNetworkType = { STATIC_DIGITAL_CABLE_NETWORK_TYPE };
			break;
		case eNetworkTypeBSkyB:
			iidNetworkType = { STATIC_BSKYB_TERRESTRIAL_TV_NETWORK_TYPE };
			break;
		case eNetworkTypeDIRECTV:
			iidNetworkType = { STATIC_DIRECT_TV_SATELLITE_TV_NETWORK_TYPE };
			break;
		case eNetworkTypeEchoStar:
			iidNetworkType = { STATIC_ECHOSTAR_SATELLITE_TV_NETWORK_TYPE };
			break;
		}

		if (it->second.nIDVBTuningSpaceSystemType != enumDVBSystemType::eDVBSystemTypeAuto) {
			dvbSystemType = (DVBSystemType)it->second.nIDVBTuningSpaceSystemType;
		}

		if (it->second.nIAnalogTVTuningSpaceInputType != enumTunerInputType::eTunerInputTypeAuto) {
			inputType = (tagTunerInputType)it->second.nIAnalogTVTuningSpaceInputType;
		}

		HRESULT hr;

		CComPtr<ITuningSpace> pITuningSpace;
		// ITuningSpace
		//
		// ITuningSpacepF
		//   ITuningSpace  IDVBTuningSpace  IDVBTuningSpace2  IDVBSTuningSpace
		//                 IAnalogTVTuningSpace  IATSCTuningSpace  IDigitalCableTuningSpace
		//                 IAnalogRadioTuningSpace  IAnalogRadioTuningSpace2
		//                 IAuxInTuningSpace  IAuxInTuningSpace2
		if (FAILED(hr = pITuningSpace.CoCreateInstance(clsidTuningSpace, NULL, CLSCTX_INPROC_SERVER))) {
			OutputDebug(L"[CreateTuningSpace] Fail to get ITuningSpace interface\n");
		}
		else {
			// ITuningSpace  NetworkType 
			if (FAILED(hr = pITuningSpace->put__NetworkType(iidNetworkType))) {
				OutputDebug(L"[CreateTuningSpace] put__NetworkType failed\n");
			}
			else {
				OutputDebug(L"[CreateTuningSpace] %s is created.\n", (wchar_t *)bstrFriendlyName);

				// ITuningSpace
				pITuningSpace->put_FrequencyMapping(L"");
				pITuningSpace->put_UniqueName(bstrUniqueName);
				pITuningSpace->put_FriendlyName(bstrFriendlyName);
				OutputDebug(L"  ITuningSpace is initialized.\n");

				// IDVBTuningSpaceL
				{
					CComQIPtr<IDVBTuningSpace> pIDVBTuningSpace(pITuningSpace);
					if (pIDVBTuningSpace) {
						pIDVBTuningSpace->put_SystemType(dvbSystemType);
						OutputDebug(L"  IDVBTuningSpace is initialized.\n");
					}
				}

				// IDVBTuningSpace2L
				{
					CComQIPtr<IDVBTuningSpace2> pIDVBTuningSpace2(pITuningSpace);
					if (pIDVBTuningSpace2) {
						pIDVBTuningSpace2->put_NetworkID(networkID);
						OutputDebug(L"  IDVBTuningSpace2 is initialized.\n");
					}
				}

				// IDVBSTuningSpaceL
				{
					CComQIPtr<IDVBSTuningSpace> pIDVBSTuningSpace(pITuningSpace);
					if (pIDVBSTuningSpace) {
						pIDVBSTuningSpace->put_HighOscillator(highOscillator);
						pIDVBSTuningSpace->put_LowOscillator(lowOscillator);
						pIDVBSTuningSpace->put_LNBSwitch(lnbSwitch);
						pIDVBSTuningSpace->put_SpectralInversion(spectralInversion);
						OutputDebug(L"  IDVBSTuningSpace is initialized.\n");
					}
				}

				// IAnalogTVTuningSpaceL
				{
					CComQIPtr<IAnalogTVTuningSpace> pIAnalogTVTuningSpace(pITuningSpace);
					if (pIAnalogTVTuningSpace) {
						pIAnalogTVTuningSpace->put_InputType(inputType);
						pIAnalogTVTuningSpace->put_MinChannel(minChannel);
						pIAnalogTVTuningSpace->put_MaxChannel(maxChannel);
						pIAnalogTVTuningSpace->put_CountryCode(countryCode);
						OutputDebug(L"  IAnalogTVTuningSpace is initialized.\n");
					}
				}

				// IATSCTuningSpaceL
				{
					CComQIPtr<IATSCTuningSpace> pIATSCTuningSpace(pITuningSpace);
					if (pIATSCTuningSpace) {
						pIATSCTuningSpace->put_MinPhysicalChannel(minPhysicalChannel);
						pIATSCTuningSpace->put_MaxPhysicalChannel(maxPhysicalChannel);
						pIATSCTuningSpace->put_MinMinorChannel(minMinorChannel);
						pIATSCTuningSpace->put_MaxMinorChannel(maxMinorChannel);
						OutputDebug(L"  IATSCTuningSpace is initialized.\n");
					}
				}

				// IDigitalCableTuningSpaceL
				{
					CComQIPtr<IDigitalCableTuningSpace> pIDigitalCableTuningSpace(pITuningSpace);
					if (pIDigitalCableTuningSpace) {
						pIDigitalCableTuningSpace->put_MinMajorChannel(minMajorChannel);
						pIDigitalCableTuningSpace->put_MaxMajorChannel(maxMajorChannel);
						pIDigitalCableTuningSpace->put_MinSourceID(minSourceID);
						pIDigitalCableTuningSpace->put_MaxSourceID(maxSourceID);
						OutputDebug(L"  IDigitalCableTuningSpace is initialized.\n");
					}
				}

				// pILocator
				//
				// ILocatorpF
				//   ILocator  IDigitalLocator  IDVBTLocator  IDVBTLocator2
				//                                IDVBSLocator  IDVBSLocator2
				//                                                IISDBSLocator
				//                                IDVBCLocator
				//                                IATSCLocator  IATSCLocator2  IDigitalCableLocator
				//             IAnalogLocator
				CComPtr<ILocator> pILocator;
				if (FAILED(hr = pILocator.CoCreateInstance(clsidLocator))) {
					OutputDebug(L"[CreateTuningSpace] Failed to get ILocator interface.\n");
				}
				else {
					// Default Locatorﾌ値 
					// ILocator
					pILocator->put_CarrierFrequency(frequency);
					pILocator->put_SymbolRate(symbolRate);
					pILocator->put_InnerFEC(innerFECMethod);
					pILocator->put_InnerFECRate(innerFECRate);
					pILocator->put_OuterFEC(outerFECMethod);
					pILocator->put_OuterFECRate(outerFECRate);
					pILocator->put_Modulation(modulationType);
					OutputDebug(L"  ILocator is initialized.\n");

					// IDigitalLocatorLvpeBﾍ豊ogﾉ残
					{
						CComQIPtr<IDigitalLocator> pIDigitalLocator(pILocator);
						if (pIDigitalLocator) {
							OutputDebug(L"  IDigitalLocator is initialized.\n");
						}
					}

					// IDVBSLocatorL
					{
						CComQIPtr<IDVBSLocator> pIDVBSLocator(pILocator);
						if (pIDVBSLocator) {
							pIDVBSLocator->put_WestPosition(westPosition);
							pIDVBSLocator->put_OrbitalPosition(orbitalPosition);
							pIDVBSLocator->put_Elevation(elevation);
							pIDVBSLocator->put_Azimuth(azimuth);
							pIDVBSLocator->put_SignalPolarisation(polarisation);
							OutputDebug(L"  IDVBSLocator is initialized.\n");
						}
					}

					// IDVBSLocator2L
					{
						CComQIPtr<IDVBSLocator2> pIDVBSLocator2(pILocator);
						if (pIDVBSLocator2) {
							pIDVBSLocator2->put_LocalOscillatorOverrideHigh(-1);
							pIDVBSLocator2->put_LocalOscillatorOverrideLow(-1);
							pIDVBSLocator2->put_LocalLNBSwitchOverride(-1);
							pIDVBSLocator2->put_LocalSpectralInversionOverride(BDA_SPECTRAL_INVERSION_NOT_SET);
							pIDVBSLocator2->put_DiseqLNBSource(diseqLNBSource);
							pIDVBSLocator2->put_SignalPilot(pilot);
							pIDVBSLocator2->put_SignalRollOff(rollOff);
							OutputDebug(L"  IDVBSLocator2 is initialized.\n");
						}
					}

					// IISDBSLocatorLvpeBﾍ豊ogﾉ残
					{
						CComQIPtr<IISDBSLocator> pIISDBSLocator(pILocator);
						if (pIISDBSLocator) {
							OutputDebug(L"  IISDBSLocator is initialized.\n");
						}
					}

					// IDVBTLocatorL
					{
						CComQIPtr<IDVBTLocator> pIDVBTLocator(pILocator);
						if (pIDVBTLocator) {
							pIDVBTLocator->put_Bandwidth(bandwidth);
							pIDVBTLocator->put_Guard(guardInterval);
							pIDVBTLocator->put_HAlpha(hierarchyAlpha);
							pIDVBTLocator->put_LPInnerFEC(lpInnerFECMethod);
							pIDVBTLocator->put_LPInnerFECRate(lpInnerFECRate);
							pIDVBTLocator->put_Mode(transmissionMode);
							pIDVBTLocator->put_OtherFrequencyInUse(otherFrequencyInUse);
							OutputDebug(L"  IDVBTLocator is initialized.\n");
						}
					}

					// IDVBTLocator2L
					{
						CComQIPtr<IDVBTLocator2> pIDVBTLocator2(pILocator);
						if (pIDVBTLocator2) {
							pIDVBTLocator2->put_PhysicalLayerPipeId(physicalLayerPipeId);
							OutputDebug(L"  IDVBTLocator2 is initialized.\n");
						}
					}

					// IDVBCLocatorLvpeBﾍ豊ogﾉ残
					{
						CComQIPtr<IDVBCLocator> pIDVBCLocator(pILocator);
						if (pIDVBCLocator) {
							OutputDebug(L"  IDVBCLocator is initialized.\n");
						}
					}

					// IATSCLocatorL
					{
						CComQIPtr<IATSCLocator> pIATSCLocator(pILocator);
						if (pIATSCLocator) {
							pIATSCLocator->put_PhysicalChannel(physicalChannel);
							pIATSCLocator->put_TSID(transportStreamID);
							OutputDebug(L"  IATSCLocator is initialized.\n");
						}
					}

					// IATSCLocator2L
					{
						CComQIPtr<IATSCLocator2> pIATSCLocator2(pILocator);
						if (pIATSCLocator2) {
							pIATSCLocator2->put_ProgramNumber(programNumber);
							OutputDebug(L"  IATSCLocator2 is initialized.\n");
						}
					}

					// IDigitalCableLocatorLvpeBﾍ豊ogﾉ残
					{
						CComQIPtr<IDigitalCableLocator> pIDigitalCableLocator(pILocator);
						if (pIDigitalCableLocator) {
							OutputDebug(L"  IDigitalCableLocator is initialized.\n");
						}
					}

					pITuningSpace->put_DefaultLocator(pILocator);

					// S
					it->second.pITuningSpace = pITuningSpace;
					OutputDebug(L"[CreateTuningSpace] Process number %ld was successful.\n", it->first);
					continue;
				}
			}
		}

		// s
		OutputDebug(L"[CreateTuningSpace] Process number %ld failed.\n", it->first);
	}

	if (!m_DVBSystemTypeDB.IsExist(0) || !m_DVBSystemTypeDB.SystemType[0].pITuningSpace) {
		OutputDebug(L"[CreateTuningSpace] Fail to create default ITuningSpace.\n");
		return E_FAIL;
	}

	return S_OK;
}

void CBonTuner::UnloadTuningSpace(void)
{
	m_DVBSystemTypeDB.ReleaseAll();
}

// Tuning Request �翌 Tuning Space 
//    output pin oﾈＡ[itB^
//   轤ｵ
HRESULT CBonTuner::InitTuningSpace(void)
{
	if (!m_DVBSystemTypeDB.IsExist(0)) {
		OutputDebug(L"[InitTuningSpace] TuningSpace NOT SET.\n");
		return E_POINTER;
	}

	if (!m_pITuner) {
		OutputDebug(L"[InitTuningSpace] ITuner NOT SET.\n");
		return E_POINTER;
	}

	HRESULT hr = E_FAIL;

	// ITuneRequest
	//
	// ITuneRequestpF
	//   ITuneRequest  IDVBTuneRequest
	//                 IChannelTuneRequest  IATSCChannelTuneRequest  IDigitalCableTuneRequest
	//                 IChannelIDTuneRequest
	//                 IMPEG2TuneRequest
	CComPtr<ITuneRequest> pITuneRequest;
	if (FAILED(hr = m_DVBSystemTypeDB.SystemType[0].pITuningSpace->CreateTuneRequest(&pITuneRequest))) {
		OutputDebug(L"[InitTuningSpace] Fail to get ITuneRequest interface.\n");
	}
	else {
		// IDVBTuneRequestL
		{
			CComQIPtr<IDVBTuneRequest> pIDVBTuneRequest(pITuneRequest);
			if (pIDVBTuneRequest) {
				pIDVBTuneRequest->put_ONID(-1);
				pIDVBTuneRequest->put_TSID(-1);
				pIDVBTuneRequest->put_SID(-1);
			}
		}

		// IChannelTuneRequestL
		{
			CComQIPtr<IChannelTuneRequest> pIChannelTuneRequest(pITuneRequest);
			if (pIChannelTuneRequest) {
				pIChannelTuneRequest->put_Channel(-1);
			}
		}

		// IATSCChannelTuneRequestL
		{
			CComQIPtr<IATSCChannelTuneRequest> pIATSCChannelTuneRequest(pITuneRequest);
			if (pIATSCChannelTuneRequest) {
				pIATSCChannelTuneRequest->put_MinorChannel(-1);
			}
		}

		// IDigitalCableTuneRequestL
		{
			CComQIPtr<IDigitalCableTuneRequest> pIDigitalCableTuneRequest(pITuneRequest);
			if (pIDigitalCableTuneRequest) {
				pIDigitalCableTuneRequest->put_MajorChannel(-1);
				pIDigitalCableTuneRequest->put_SourceID(-1);
			}
		}

		hr = m_pITuner->put_TuningSpace(m_DVBSystemTypeDB.SystemType[0].pITuningSpace);
		hr = m_pITuner->put_TuneRequest(pITuneRequest);

		// S
		return S_OK;
	}

	// s
	return hr;
}

HRESULT CBonTuner::LoadNetworkProvider(void)
{
	CLSID clsidNetworkProvider = CLSID_NULL;

	switch (m_nNetworkProvider) {
	case eNetworkProviderGeneric:
		clsidNetworkProvider = CLSID_NetworkProvider;
		break;
	case eNetworkProviderDVBS:
		clsidNetworkProvider = CLSID_DVBSNetworkProvider;
		break;
	case eNetworkProviderDVBT:
		clsidNetworkProvider = CLSID_DVBTNetworkProvider;
		break;
	case eNetworkProviderDVBC:
		clsidNetworkProvider = CLSID_DVBCNetworkProvider;
		break;
	case eNetworkProviderATSC:
		clsidNetworkProvider = CLSID_ATSCNetworkProvider;
		break;
	case eNetworkProviderAuto:
	default:
		if (m_DVBSystemTypeDB.nNumType > 1) {
			clsidNetworkProvider = CLSID_NetworkProvider;
		}
		else {
			switch (m_DVBSystemTypeDB.SystemType[0].nDVBSystemType) {
			case eTunerTypeDVBS:
			case eTunerTypeISDBS:
				clsidNetworkProvider = CLSID_DVBSNetworkProvider;
				break;
			case eTunerTypeDVBT:
			case eTunerTypeDVBT2:
			case eTunerTypeISDBT:
				clsidNetworkProvider = CLSID_DVBTNetworkProvider;
				break;
			case eTunerTypeDVBC:
				clsidNetworkProvider = CLSID_DVBCNetworkProvider;
				break;
			case eTunerTypeATSC_Antenna:
			case eTunerTypeATSC_Cable:
			case eTunerTypeDigitalCable:
				clsidNetworkProvider = CLSID_ATSCNetworkProvider;
				break;
			default:
				clsidNetworkProvider = CLSID_NetworkProvider;
				break;
			}
		}
		break;
	}

	HRESULT hr = E_FAIL;

	CComPtr<IBaseFilter> pNetworkProvider;
	// Network ProveidertB^謫ｾ
	if (FAILED(hr = pNetworkProvider.CoCreateInstance(clsidNetworkProvider, NULL, CLSCTX_INPROC_SERVER))) {
		OutputDebug(L"[LoadNetworkProvider] Fail to get NetworkProvider IBaseFilter interface.\n");
	}
	else {
		std::wstring strName = CDSFilterEnum::getRegistryName(pNetworkProvider);
		OutputDebug(L"[LoadNetworkProvider] %s is loaded.\n", strName.c_str());
		// tB^謫ｾ
		// Graph BuilderﾉフB^
		if (FAILED(hr = m_pIGraphBuilder->AddFilter(pNetworkProvider, strName.c_str()))) {
			OutputDebug(L"[LoadNetworkProvider] Fail to add NetworkProvider IBaseFilter into graph.\n");
		}
		else {
			// tB^
			// ITuner interface謫ｾ
			CComQIPtr<ITuner> pITuner(pNetworkProvider);
			if (!pITuner) {
				OutputDebug(L"[LoadNetworkProvider] Fail to get ITuner interface.\n");
				hr = E_FAIL;
			}
			else {
				// ITuner interfaceﾌ取得
				// S
				m_pNetworkProvider = pNetworkProvider;
				m_pITuner = pITuner;
				return hr;
			}
		}
	}

	// s
	return hr;
}

void CBonTuner::UnloadNetworkProvider(void)
{
	HRESULT hr;
	if (m_pIGraphBuilder && m_pNetworkProvider)
		hr = m_pIGraphBuilder->RemoveFilter(m_pNetworkProvider);

	m_pITuner.Release();
	m_pNetworkProvider.Release();
}

// ini t@Cﾅ指閧ｳ黷ｽ`[iELv`ﾌ組List
HRESULT CBonTuner::InitDSFilterEnum(void)
{
	HRESULT hr;

	// VXeﾉ貸Ａ[iELv`ﾌスg
	std::vector<DSListData> TunerList;
	std::vector<DSListData> CaptureList;

	ULONG order;

	SAFE_DELETE(m_pDSFilterEnumTuner);
	SAFE_DELETE(m_pDSFilterEnumCapture);

	m_pDSFilterEnumTuner = new CDSFilterEnum(KSCATEGORY_BDA_NETWORK_TUNER, CDEF_DEVMON_PNP_DEVICE);
	order = 0;
	while (SUCCEEDED(hr = m_pDSFilterEnumTuner->next()) && hr == S_OK) {
		std::wstring sDisplayName;
		std::wstring sFriendlyName;

		// `[i DisplayName, FriendlyName 
		m_pDSFilterEnumTuner->getDisplayName(&sDisplayName);
		m_pDSFilterEnumTuner->getFriendlyName(&sFriendlyName);

		// 齬遼?
		TunerList.emplace_back(sDisplayName, sFriendlyName, order);

		order++;
	}

	m_pDSFilterEnumCapture = new CDSFilterEnum(KSCATEGORY_BDA_RECEIVER_COMPONENT, CDEF_DEVMON_PNP_DEVICE);
	order = 0;
	while (SUCCEEDED(hr = m_pDSFilterEnumCapture->next()) && hr == S_OK) {
		std::wstring sDisplayName;
		std::wstring sFriendlyName;

		// `[i DisplayName, FriendlyName 
		m_pDSFilterEnumCapture->getDisplayName(&sDisplayName);
		m_pDSFilterEnumCapture->getFriendlyName(&sFriendlyName);

		// 齬遼?
		CaptureList.emplace_back(sDisplayName, sFriendlyName, order);

		order++;
	}

	unsigned int total = 0;
	m_UsableTunerCaptureList.clear();

	for (unsigned int i = 0; i < m_aTunerParam.Tuner.size(); i++) {
		for (auto it = TunerList.begin(); it != TunerList.end(); it++) {
			// DisplayName  GUID ﾜまるかﾄ、NO邇淮チ[i
			if (m_aTunerParam.Tuner[i].TunerGUID.compare(L"") != 0 && it->GUID.find(m_aTunerParam.Tuner[i].TunerGUID) == std::wstring::npos) {
				continue;
			}

			// FriendlyName ﾜまるかﾄ、NO邇淮チ[i
			if (m_aTunerParam.Tuner[i].TunerFriendlyName.compare(L"") != 0 && it->FriendlyName.find(m_aTunerParam.Tuner[i].TunerFriendlyName) == std::wstring::npos) {
				continue;
			}

			// ﾎ象のチ[ifoCX
			OutputDebug(L"[InitDSFilterEnum] Found tuner device=FriendlyName:%s,  GUID:%s\n", it->FriendlyName.c_str(), it->GUID.c_str());
			if (!m_aTunerParam.bNotExistCaptureDevice) {
				// CapturefoCXgp
				std::vector<DSListData> TempCaptureList;
				for (auto it2 = CaptureList.begin(); it2 != CaptureList.end(); it2++) {
					// DisplayName  GUID ﾜまるかﾄ、NO邇淮キv`
					if (m_aTunerParam.Tuner[i].CaptureGUID.compare(L"") != 0 && it2->GUID.find(m_aTunerParam.Tuner[i].CaptureGUID) == std::wstring::npos) {
						continue;
					}

					// FriendlyName ﾜまるかﾄ、NO邇淮キv`
					if (m_aTunerParam.Tuner[i].CaptureFriendlyName.compare(L"") != 0 && it2->FriendlyName.find(m_aTunerParam.Tuner[i].CaptureFriendlyName) == std::wstring::npos) {
						continue;
					}

					// ﾎ象のキv`foCX
					OutputDebug(L"[InitDSFilterEnum]   Found capture device=FriendlyName:%s,  GUID:%s\n", it2->FriendlyName.c_str(), it2->GUID.c_str());
					TempCaptureList.emplace_back(*it2);
				}

				if (TempCaptureList.empty()) {
					// Lv`foCXﾂなので捨チ[i
					OutputDebug(L"[InitDSFilterEnum]   No combined capture devices.\n");
					continue;
				}

				// `[iListﾉ?
				m_UsableTunerCaptureList.emplace_back(*it);

				unsigned int count = 0;
				if (m_aTunerParam.bCheckDeviceInstancePath) {
					// `[ifoCXﾆキv`foCXﾌデoCXCX^XpXvﾄるかmF
					OutputDebug(L"[InitDSFilterEnum]   Checking device instance path.\n");
					std::wstring dip = CDSFilterEnum::getDeviceInstancePathrFromDisplayName(it->GUID);
					for (auto it2 = TempCaptureList.begin(); it2 != TempCaptureList.end(); it2++) {
						if (CDSFilterEnum::getDeviceInstancePathrFromDisplayName(it2->GUID) == dip) {
							// foCXpXvListﾉ?
							OutputDebug(L"[InitDSFilterEnum]     Adding matched tuner and capture device.\n");
							OutputDebug(L"[InitDSFilterEnum]       tuner=FriendlyName:%s,  GUID:%s\n", it->FriendlyName.c_str(), it->GUID.c_str());
							OutputDebug(L"[InitDSFilterEnum]       capture=FriendlyName:%s,  GUID:%s\n", it2->FriendlyName.c_str(), it2->GUID.c_str());
							m_UsableTunerCaptureList.back().CaptureList.emplace_back(*it2);
							count++;
						}
					}
				}

				if (count == 0) {
					// foCXpXvﾌ? or mF
					if (m_aTunerParam.bCheckDeviceInstancePath) {
						OutputDebug(L"[InitDSFilterEnum]     No matched devices.\n");
					}
					for (auto it2 = TempCaptureList.begin(); it2 != TempCaptureList.end(); it2++) {
						// Listﾉ?
						OutputDebug(L"[InitDSFilterEnum]   Adding tuner and capture device.\n");
						OutputDebug(L"[InitDSFilterEnum]     tuner=FriendlyName:%s,  GUID:%s\n", it->FriendlyName.c_str(), it->GUID.c_str());
						OutputDebug(L"[InitDSFilterEnum]     capture=FriendlyName:%s,  GUID:%s\n", it2->FriendlyName.c_str(), it2->GUID.c_str());
						m_UsableTunerCaptureList.back().CaptureList.emplace_back(*it2);
						count++;
					}
				}

				OutputDebug(L"[InitDSFilterEnum]   %d of combination was added.\n", count);
				total += count;
			}
			else
			{
				// CapturefoCXgp
				OutputDebug(L"[InitDSFilterEnum]   Adding tuner device only.\n");
				OutputDebug(L"[InitDSFilterEnum]     tuner=FriendlyName:%s,  GUID:%s\n", it->FriendlyName.c_str(), it->GUID.c_str());
				m_UsableTunerCaptureList.emplace_back(*it);
			}
		}
	}
	if (m_UsableTunerCaptureList.empty()) {
		OutputDebug(L"[InitDSFilterEnum] No devices found.\n");
		return E_FAIL;
	}

	OutputDebug(L"[InitDSFilterEnum] Total %d of combination was added.\n", total);
	return S_OK;
}

// `[iELv`ﾌ組墲ｹXg逑ｮﾌ５
HRESULT CBonTuner::LoadAndConnectDevice(void)
{
	HRESULT hr;
	if (!m_DVBSystemTypeDB.IsExist(0) || !m_pNetworkProvider) {
		OutputDebug(L"[P->T] TuningSpace or NetworkProvider NOT SET.\n");
		return E_POINTER;
	}

	if (!m_pDSFilterEnumTuner || (!m_pDSFilterEnumCapture && !m_aTunerParam.bNotExistCaptureDevice)) {
		OutputDebug(L"[P->T] DSFilterEnum NOT SET.\n");
		return E_POINTER;
	}

	for (auto it = m_UsableTunerCaptureList.begin(); it != m_UsableTunerCaptureList.end(); it++) {
		OutputDebug(L"[P->T] Trying tuner device=FriendlyName:%s,  GUID:%s\n", it->Tuner.FriendlyName.c_str(), it->Tuner.GUID.c_str());
		// `[ifoCX[v
		// rpﾉセ}tHp ('\' -> '/')
		std::wstring semName = it->Tuner.GUID;
		std::replace(semName.begin(), semName.end(), L'\\', L'/');
		semName = L"Global\\" + semName;

		// r
		m_hSemaphore = ::CreateSemaphoreW(NULL, 1, 1, semName.c_str());
		DWORD result = WaitForSingleObjectWithMessageLoop(m_hSemaphore, 0);
		if (result != WAIT_OBJECT_0) {
			// gpﾈので捨チ[iT
			OutputDebug(L"[P->T] Another is using.\n");
		} 
		else {
			// rmFOK
			CComPtr<IBaseFilter> pTunerDevice;
			// `[ifoCXﾌフB^謫ｾ
			if (FAILED(hr = m_pDSFilterEnumTuner->getFilter(&pTunerDevice, it->Tuner.Order))) {
				// tB^謫ｾﾅなので捨チ[i
				OutputDebug(L"[P->T] Fail to get TunerDevice IBaseFilter interface.\n");
			}
			else {
				// tB^謫ｾ
				// Graph Builderﾉチ[ifoCXﾌフB^
				if (FAILED(hr = m_pIGraphBuilder->AddFilter(pTunerDevice, it->Tuner.FriendlyName.c_str()))) {
					// tB^ﾌ追暇市ﾌで捨チ[i
					OutputDebug(L"[P->T] Fail to add TunerDevice IBaseFilter into graph.\n");
				}
				else {
					// tB^
					// `[ifoCXconnect ﾄ?
					if (FAILED(hr = Connect(m_pNetworkProvider, pTunerDevice))) {
						// connectﾉ市ﾌで捨チ[i
						OutputDebug(L"[P->T] Fail to connect.\n");
					}
					else {
						// connect 
						OutputDebug(L"[P->T] Connect OK.\n");
						// `[iﾅ有DllKvﾈど在、ﾅ有ﾌ笑呼び出
						if (FAILED(hr = CheckAndInitTunerDependDll(pTunerDevice, it->Tuner.GUID, it->Tuner.FriendlyName))) {
							// ﾅ有Dllﾌ峻ﾌで捨チ[i
							OutputDebug(L"[P->T] Discarded by BDASpecial's CheckAndInitTuner function.\n");
						}
						else {
							// ﾅ有DllOK
							if (!m_aTunerParam.bNotExistCaptureDevice) {
								// Lv`foCXgp
								for (auto it2 = it->CaptureList.begin(); it2 != it->CaptureList.end(); it2++) {
									// Lv`foCX[v
									OutputDebug(L"[T->C] Trying capture device=FriendlyName:%s,  GUID:%s\n", it2->FriendlyName.c_str(), it2->GUID.c_str());
									// `[iﾅ有Dllﾅの確Fﾎ呼び出
									if (FAILED(hr = CheckCapture(it->Tuner.GUID, it->Tuner.FriendlyName, it2->GUID, it2->FriendlyName))) {
										// ﾅ有Dll_ﾆ呼ので捨キv`foCX
										OutputDebug(L"[T->C] Discarded by BDASpecial's CheckCapture function.\n");
									}
									else {
										// ﾅ有Dllﾌ確FOK
										CComPtr<IBaseFilter> pCaptureDevice;
										// Lv`foCXﾌフB^謫ｾ
										if (!m_pDSFilterEnumCapture || FAILED(hr = m_pDSFilterEnumCapture->getFilter(&pCaptureDevice, it2->Order))) {
											// tB^謫ｾﾅなので捨キv`foCX
											OutputDebug(L"[T->C] Fail to get CaptureDevice IBaseFilter interface.\n");
										}
										else {
											// tB^謫ｾ
											// Graph Builderﾉキv`foCXﾌフB^
											if (FAILED(hr = m_pIGraphBuilder->AddFilter(pCaptureDevice, it2->FriendlyName.c_str()))) {
												// tB^ﾌ追暇市ﾌで捨キv`foCX
												OutputDebug(L"[T->C] Fail to add CaptureDevice IBaseFilter into graph.\n");
											}
											else {
												// tB^
												// Lv`foCXconnect ﾄ?
												if (FAILED(hr = Connect(pTunerDevice, pCaptureDevice))) {
													// connectﾉ市ﾌで捨キv`foCX
													OutputDebug(L"[T->C] Fail to connect.\n");
												}
												else {
													// connect 
													OutputDebug(L"[T->C] Connect OK.\n");
													// TsWriterﾈ降ﾆ接疏Run
													if (SUCCEEDED(LoadAndConnectMiscFilters(pTunerDevice, pCaptureDevice))) {
														// ﾗ?
														m_pTunerDevice = pTunerDevice;
														m_pCaptureDevice = pCaptureDevice;
														// `[iﾅ有ﾖ昔ゼh
														LoadTunerDependCode(it->Tuner.GUID, it->Tuner.FriendlyName, it2->GUID, it2->FriendlyName);
														if (m_bTryAnotherTuner)
															// ﾌ組`[iELv`Xgﾌ最庫?
															m_UsableTunerCaptureList.splice(m_UsableTunerCaptureList.end(), m_UsableTunerCaptureList, it);
														return S_OK;
													}
													// Lv`foCXdisconnect
													// DisconnectAll(pCaptureDevice);
												}
												// Graph BuilderLv`foCXremove
												m_pIGraphBuilder->RemoveFilter(pCaptureDevice);
											}
										}
									}
									// ﾌキv`foCXﾖゼv
								}
								// Lv`foCX[vI
								// gﾂなので捨チ[i
							}
							else {
								// Lv`foCXgpﾈれ合
								// TsWriterﾈ降ﾆ接疏Run
								if (SUCCEEDED(hr = LoadAndConnectMiscFilters(pTunerDevice, NULL))) {
									// ﾗ?
									m_pTunerDevice = pTunerDevice;
									// `[iﾅ有ﾖ昔ゼh
									LoadTunerDependCode(it->Tuner.GUID, it->Tuner.FriendlyName, L"", L"");
									if (m_bTryAnotherTuner)
										// ﾌ組`[iELv`Xgﾌ最庫?
										m_UsableTunerCaptureList.splice(m_UsableTunerCaptureList.end(), m_UsableTunerCaptureList, it);
									return S_OK;
								}
							}
							// `[iﾅ有ﾖ織ll
							ReleaseTunerDependCode();
						}
						// Graph Builder`[iremove
						// DisconnectAll(pTunerDevice);
					}
					// Graph Builder`[iremove
					m_pIGraphBuilder->RemoveFilter(pTunerDevice);
				}
			}
			// rI
			::ReleaseSemaphore(m_hSemaphore, 1, NULL);
		}
		// Z}tH
		SAFE_CLOSE_HANDLE(m_hSemaphore);
		// ﾌチ[ifoCXﾖゼv
	}
	// `[ifoCX[vI
	// gﾝ削せﾂ?
	OutputDebug(L"[P->T] Can not found a connectable pair of TunerDevice and CaptureDevice.\n");
	return E_FAIL;
}

void CBonTuner::UnloadTunerDevice(void)
{
	HRESULT hr;

	ReleaseTunerDependCode();

	if (m_pIGraphBuilder && m_pTunerDevice)
		hr = m_pIGraphBuilder->RemoveFilter(m_pTunerDevice);

	m_pTunerDevice.Release();
}

void CBonTuner::UnloadCaptureDevice(void)
{
	HRESULT hr;

	if (m_pIGraphBuilder && m_pCaptureDevice)
		hr = m_pIGraphBuilder->RemoveFilter(m_pCaptureDevice);

	m_pCaptureDevice.Release();
}

HRESULT CBonTuner::LoadAndConnectMiscFilters(IBaseFilter* pTunerDevice, IBaseFilter* pCaptureDevice)
{
	HRESULT hr = E_FAIL;

	// TsWriter縺ｨ謗･
	if (SUCCEEDED(hr = LoadAndConnectTsWriter(pTunerDevice, pCaptureDevice))) {
		// Demux 縺ｨ TIF 繧呈磁
		if (SUCCEEDED(hr = LoadAndConnectDemux())) {
			if (SUCCEEDED(hr = LoadAndConnectTif())) {
				OutputDebug(L"LoadAndConnectMiscFilters: Connected TsWriter + Demux + TIF.\n");
				// Try terminating remaining unconnected output pins of Tuner with NullRenderer
				{
					static constexpr CLSID CLSID_NullRenderer_Local = { 0xc1f400a0, 0x3f08, 0x11d3, { 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37 } };
					// Do NOT terminate unused pins with NullRenderer: AVStream fails Pause on uninitialized pipes!
					OutputDebug(L"LoadAndConnectMiscFilters: Unused output pins left untouched.\n");
				}
				// 荳譌ｦRun繧定ｩｦ縺励※縺ｿ繧九′縲∝､ｱ謨励＠縺ｦ繧ゅメ繝｣繝ｳ繝阪Ν譛ｪ險ｭ螳壹◆繧∬ｨｱ螳ｹ縺吶ｋ
				return S_OK;
			}
			UnloadDemux();
		}

		OutputDebug(L"LoadAndConnectMiscFilters: Connected TsWriter only.\n");

		return S_OK;
	}

	return hr;
}

HRESULT CBonTuner::LoadAndConnectTsWriter(IBaseFilter* pTunerDevice, IBaseFilter* pCaptureDevice)
{
	OutputDebug(L"--- Diagnosing Tuner Pins & MediaTypes ---\n");
	if (pTunerDevice) {
		CDSEnumPins enumPins(pTunerDevice);
		CComPtr<IPin> pPin;
		while (S_OK == enumPins.getNextPin(&pPin)) {
			PIN_INFO pinfo = {0};
			pPin->QueryPinInfo(&pinfo);
			if (pinfo.pFilter) pinfo.pFilter->Release();
			PIN_DIRECTION pdir;
			pPin->QueryDirection(&pdir);
			GUID pinCat = GUID_NULL;
			CComPtr<IKsPropertySet> pKsPin;
			if (SUCCEEDED(pPin->QueryInterface(IID_IKsPropertySet, (void**)&pKsPin))) {
				DWORD cbRet = 0;
				pKsPin->Get(KSPROPSETID_Pin, KSPROPERTY_PIN_CATEGORY, NULL, 0, &pinCat, sizeof(GUID), &cbRet);
			}
			OutputDebug(L"  Pin [%s] Direction: %s, Cat: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n",
				pinfo.achName, (pdir == PINDIR_INPUT) ? L"INPUT" : L"OUTPUT",
				pinCat.Data1, pinCat.Data2, pinCat.Data3,
				pinCat.Data4[0], pinCat.Data4[1], pinCat.Data4[2], pinCat.Data4[3],
				pinCat.Data4[4], pinCat.Data4[5], pinCat.Data4[6], pinCat.Data4[7]);

			CComPtr<IEnumMediaTypes> pEnumMT;
			if (SUCCEEDED(pPin->EnumMediaTypes(&pEnumMT)) && pEnumMT) {
				AM_MEDIA_TYPE *pmt = NULL;
				ULONG fetched = 0;
				int mtCount = 0;
				while (pEnumMT->Next(1, &pmt, &fetched) == S_OK && fetched > 0) {
					LPOLESTR pMajor = NULL, pSub = NULL;
					StringFromIID(pmt->majortype, &pMajor);
					StringFromIID(pmt->subtype, &pSub);
					OutputDebug(L"    MT[%d]: Major=%s Sub=%s sampleSize=%u\n", mtCount++, pMajor, pSub, pmt->lSampleSize);
					CoTaskMemFree(pMajor);
					CoTaskMemFree(pSub);
					if (pmt->cbFormat > 0 && pmt->pbFormat) CoTaskMemFree(pmt->pbFormat);
					CoTaskMemFree(pmt);
				}
			}
			pPin.Release();
		}
	}
	OutputDebug(L"-------------------------------------------\n");

	// TS Writer:AddFilterﾉ登^髢ｼO
	static constexpr WCHAR * const FILTER_GRAPH_NAME_TSWRITER = L"TS Writer";

	HRESULT hr = E_FAIL;

	if (!pTunerDevice || (!pCaptureDevice && !m_aTunerParam.bNotExistCaptureDevice)) {
		OutputDebug(L"[C->W/T->W] TunerDevice or CaptureDevice NOT SET.\n");
		return E_POINTER;
	}

	// tB^NXﾌゼh
	CTsWriter *pCTsWriter = (CTsWriter *)CTsWriter::CreateInstance(NULL, &hr);
	if (!pCTsWriter) {
		OutputDebug(L"[C->W/T->W] Fail to create CTsWriter filter class instance.\n");
		hr = E_FAIL;
	}
	else {
		// tB^謫ｾ
		CComQIPtr<IBaseFilter> pTsWriter(pCTsWriter);
		if (!pTsWriter) {
			OutputDebug(L"[C->W/T->W] Fail to get TsWriter IBaseFilter interface.\n");
			hr = E_FAIL;
		}
		else {
			// tB^謫ｾ
			OutputDebug(L"[C->W/T->W] %s is loaded.\n", FILTER_GRAPH_NAME_TSWRITER);
			// Graph BuilderﾉフB^
			if (FAILED(hr = m_pIGraphBuilder->AddFilter(pTsWriter, FILTER_GRAPH_NAME_TSWRITER))) {
				OutputDebug(L"[C->W/T->W] Fail to add TsWriter IBaseFilter into graph.\n");
			}
			else {
				// tB^
				// ITsWriter interface謫ｾ
				CComQIPtr<ITsWriter> pITsWriter(pTsWriter);
				if (!pITsWriter) {
					OutputDebug(L"[C->W/T->W] Fail to get ITsWriter interface.\n");
					hr = E_FAIL;
				}
				else {
					// ITsWriter interfaceﾌ取得
					// connect ﾄ?
					if (m_aTunerParam.bNotExistCaptureDevice) {
						// Connect optional secondary stream pin (e.g. Stream Out2) before primary Stream Out
						wchar_t szSubPin[128] = {0};
						GetPrivateProfileString(L"Tuner", L"SubstreamPinName", L"", szSubPin, 128, m_sIniFilePath.c_str());
						if (szSubPin[0] != L'\0') {
							static constexpr CLSID CLSID_NullRenderer_Local = { 0xc1f400a0, 0x3f08, 0x11d3, { 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37 } };
							CDSEnumPins enumSub(pTunerDevice);
							CComPtr<IPin> pPinSub;
							while (S_OK == enumSub.getNextPin(&pPinSub, PIN_DIRECTION::PINDIR_OUTPUT)) {
								PIN_INFO piSub = {0};
								pPinSub->QueryPinInfo(&piSub);
								if (piSub.pFilter) piSub.pFilter->Release();
								CComPtr<IPin> pPeer;
								if (wcscmp(piSub.achName, szSubPin) == 0 && pPinSub->ConnectedTo(&pPeer) == VFW_E_NOT_CONNECTED) {
									CComPtr<IBaseFilter> pNull;
									HRESULT hrSub = CoCreateInstance(CLSID_NullRenderer_Local, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&pNull);
									if (SUCCEEDED(hrSub) && pNull) {
										hrSub = m_pIGraphBuilder->AddFilter(pNull, L"Substream Sink");
										if (SUCCEEDED(hrSub)) {
											CDSEnumPins enumNull(pNull);
											CComPtr<IPin> pNullIn;
											if (S_OK == enumNull.getNextPin(&pNullIn, PIN_DIRECTION::PINDIR_INPUT)) {
												hrSub = m_pIGraphBuilder->ConnectDirect(pPinSub, pNullIn, NULL);
											} else {
												hrSub = E_FAIL;
											}
											if (SUCCEEDED(hrSub)) {
												m_pSubstreamSink = pNull;
												OutputDebug(L"  Connected Substream Pin [%s] -> [Substream Sink] SUCCESS!\n", szSubPin);
											} else {
												m_pIGraphBuilder->RemoveFilter(pNull);
												OutputDebug(L"  ConnectDirect Substream Pin [%s] failed hr=0x%08X\n", szSubPin, hrSub);
											}
										}
									} else {
										OutputDebug(L"  CoCreateInstance NullRenderer for [%s] failed hr=0x%08X\n", szSubPin, hrSub);
									}
									break;
								}
								pPinSub.Release();
							}
						}
						// CapturefoCXﾝなれ合Tunerﾆ?
						if (FAILED(hr = Connect(pTunerDevice, pTsWriter))) {
							OutputDebug(L"[T->W] Failed to connect.\n");
							UnloadSubstreamSink();
						}
					}
					else {
						// CapturefoCXﾆ?
						if (FAILED(hr = Connect(pCaptureDevice, pTsWriter))) {
							OutputDebug(L"[C->W] Fail to connect.\n");
						}
					}
					if (SUCCEEDED(hr)) {
						// connect ﾈのでのまま終
						OutputDebug(L"[C->W/T->W] Connect OK.\n");
						m_pTsWriter = pTsWriter;
						m_pITsWriter = pITsWriter;
						return hr;
					}
				}
				m_pIGraphBuilder->RemoveFilter(pTsWriter);
			}
		}
	}

	// s
	return hr;
}

void CBonTuner::UnloadTsWriter(void)
{
	HRESULT hr;
	if (m_pIGraphBuilder && m_pTsWriter)
		hr = m_pIGraphBuilder->RemoveFilter(m_pTsWriter);

	m_pITsWriter.Release();
	m_pTsWriter.Release();
}

void CBonTuner::UnloadSubstreamSink(void)
{
	if (m_pIGraphBuilder && m_pSubstreamSink)
		m_pIGraphBuilder->RemoveFilter(m_pSubstreamSink);
	m_pSubstreamSink.Release();
}

HRESULT CBonTuner::LoadAndConnectDemux(void)
{
	HRESULT hr = E_FAIL;

	if (!m_pTsWriter) {
			OutputDebug(L"[W->M] TsWriter NOT SET.\n");
			return E_POINTER;
	}

	CComPtr<IBaseFilter> pDemux;
	// tB^謫ｾ
	if (FAILED(hr = pDemux.CoCreateInstance(CLSID_MPEG2Demultiplexer, NULL, CLSCTX_INPROC_SERVER))) {
		OutputDebug(L"[W->M] Fail to get MPEG2Demultiplexer IBaseFilter interface.\n");
	}
	else {
		std::wstring strName = CDSFilterEnum::getRegistryName(pDemux);
		OutputDebug(L"[W->M] %s is loaded.\n", strName.c_str());
		// tB^謫ｾ
		// Graph BuilderﾉフB^
		if (FAILED(hr = m_pIGraphBuilder->AddFilter(pDemux, strName.c_str()))) {
			OutputDebug(L"[W->M] Fail to add MPEG2Demultiplexer IBaseFilter into graph.\n");
		}
		else {
			// tB^
			// connect ﾄ?
			if (FAILED(hr = Connect(m_pTsWriter, pDemux))) {
				OutputDebug(L"[W->M] Fail to connect.\n");
			}
			else {
				// connect ﾈのでのまま終
				OutputDebug(L"[W->M] Connect OK.\n");
				m_pDemux = pDemux;
				return hr;
			}
			m_pIGraphBuilder->RemoveFilter(pDemux);
		}
	}

	// s
	return hr;
}

void CBonTuner::UnloadDemux(void)
{
	HRESULT hr;
	if (m_pIGraphBuilder && m_pDemux)
		hr = m_pIGraphBuilder->RemoveFilter(m_pDemux);

	m_pDemux.Release();
}

HRESULT CBonTuner::LoadAndConnectTif(void)
{
	// MPEG2 TIFCLSID
	static constexpr CLSID CLSID_MPEG2TransportInformationFilter = { 0xfc772ab0, 0x0c7f, 0x11d3, 0x8f, 0xf2, 0x00, 0xa0, 0xc9, 0x22, 0x4c, 0xf4 };

	HRESULT hr = E_FAIL;

	if (!m_pDemux) {
			OutputDebug(L"[M->I] MPEG2Demultiplexer NOT SET.\n");
			return E_POINTER;
	}

	CComPtr<IBaseFilter> pTif;
	// tB^謫ｾ
	if (FAILED(hr = pTif.CoCreateInstance(CLSID_MPEG2TransportInformationFilter, NULL, CLSCTX_INPROC_SERVER))) {
		OutputDebug(L"[M->I] Fail to get TIF IBaseFilter interface.\n");
	}
	else {
		std::wstring strName = CDSFilterEnum::getRegistryName(pTif);
		OutputDebug(L"[M->I] %s is loaded.\n", strName.c_str());
		// tB^謫ｾ
		// Graph BuilderﾉフB^
		if (FAILED(hr = m_pIGraphBuilder->AddFilter(pTif, strName.c_str()))) {
			OutputDebug(L"[M->I] Fail to add TIF IBaseFilter into graph.\n");
		}
		else {
			// tB^
			// connect ﾄ?
			if (FAILED(hr = Connect(m_pDemux, pTif))) {
				OutputDebug(L"[M->I] Fail to connect.\n");
			}
			else {
				// connect ﾈのでのまま終
				OutputDebug(L"[M->I] Connect OK.\n");
				m_pTif = pTif;
				return hr;
			}
			m_pIGraphBuilder->RemoveFilter(pTif);
		}
	}

	// s
	return hr;
}

void CBonTuner::UnloadTif(void)
{
	HRESULT hr;
	if (m_pIGraphBuilder && m_pTif)
		hr = m_pIGraphBuilder->RemoveFilter(m_pTif);

	m_pTif.Release();
}

HRESULT CBonTuner::LoadTunerSignalStatisticsTunerNode(void)
{
	HRESULT hr = E_FAIL;

	if (!m_pTunerDevice) {
		OutputDebug(L"[LoadTunerSignalStatisticsTunerNode] TunerDevice NOT SET.\n");
		return E_POINTER;
	}

	CDSEnumNodes DSEnumNodes(m_pTunerDevice);
	CComPtr<IUnknown> pControlNode;
	if (FAILED(hr = DSEnumNodes.getControlNode(__uuidof(IBDA_FrequencyFilter), &pControlNode))) {
		OutputDebug(L"[LoadTunerSignalStatisticsTunerNode] Fail to get control node.\n");
		return E_FAIL;
	}

	CComQIPtr<IBDA_SignalStatistics> pIBDA_SignalStatistics(pControlNode);
	if (!pIBDA_SignalStatistics) {
		OutputDebug(L"[LoadTunerSignalStatisticsTunerNode] Fail to get IBDA_SignalStatistics interface.\n");
		return E_FAIL;
	}

	OutputDebug(L"[LoadTunerSignalStatisticsTunerNode] SUCCESS.\n");
	{
		static const GUID PROPSET_TUNER = { 0x742F68D0, 0x0CCA, 0x3CDF, { 0xFA, 0xEA, 0x6A, 0x06, 0xF4, 0x74, 0xFC, 0xEE } };
		CComQIPtr<IKsPropertySet> pKsProp(pControlNode);
		if (pKsProp) {
			DWORD dwSupp = 0;
			HRESULT hrSupp = pKsProp->QuerySupported(PROPSET_TUNER, 1, &dwSupp);
			OutputDebug(L"  [TunerNode] QuerySupported(PROPSET_TUNER, 1): hr=0x%08X, supp=0x%X\n", hrSupp, dwSupp);
		} else {
			OutputDebug(L"  [TunerNode] IKsPropertySet NOT supported.\n");
		}
	}
	m_pIBDA_FrequencyFilter = CComQIPtr<IBDA_FrequencyFilter>(pControlNode);
	if (m_pIBDA_FrequencyFilter) {
		OutputDebug(L"  [TunerNode] IBDA_FrequencyFilter interface obtained!\n");
	}
	m_pIBDA_SignalStatisticsTunerNode = pIBDA_SignalStatistics;

	return S_OK;
}

HRESULT CBonTuner::LoadTunerSignalStatisticsDemodNode(void)
{
	HRESULT hr = E_FAIL;

	if (!m_pTunerDevice) {
		OutputDebug(L"[LoadTunerSignalStatisticsDemodNode] TunerDevice NOT SET.\n");
		return E_POINTER;
	}

	CDSEnumNodes DSEnumNodes(m_pTunerDevice);
	CComPtr<IUnknown> pControlNode;
	if (FAILED(hr = DSEnumNodes.getControlNode(__uuidof(IBDA_DigitalDemodulator), &pControlNode))) {
		OutputDebug(L"[LoadTunerSignalStatisticsDemodNode] Fail to get control node.\n");
		return E_FAIL;
	}

	CComQIPtr<IBDA_SignalStatistics> pIBDA_SignalStatistics(pControlNode);
	if (!pIBDA_SignalStatistics) {
		OutputDebug(L"[LoadTunerSignalStatisticsDemodNode] Fail to get IBDA_SignalStatistics interface.\n");
		return E_FAIL;
	}

	OutputDebug(L"[LoadTunerSignalStatisticsDemodNode] SUCCESS.\n");
	{
		static const GUID PROPSET_TUNER = { 0x742F68D0, 0x0CCA, 0x3CDF, { 0xFA, 0xEA, 0x6A, 0x06, 0xF4, 0x74, 0xFC, 0xEE } };
		CComQIPtr<IKsPropertySet> pKsProp(pControlNode);
		if (pKsProp) {
			DWORD dwSupp = 0;
			HRESULT hrSupp = pKsProp->QuerySupported(PROPSET_TUNER, 1, &dwSupp);
			OutputDebug(L"  [DemodNode] QuerySupported(PROPSET_TUNER, 1): hr=0x%08X, supp=0x%X\n", hrSupp, dwSupp);
		} else {
			OutputDebug(L"  [DemodNode] IKsPropertySet NOT supported.\n");
		}
	}
	m_pIBDA_DigitalDemodulator = CComQIPtr<IBDA_DigitalDemodulator>(pControlNode);
	if (m_pIBDA_DigitalDemodulator) {
		OutputDebug(L"  [DemodNode] IBDA_DigitalDemodulator interface obtained!\n");
	}
	m_pIBDA_SignalStatisticsDemodNode = pIBDA_SignalStatistics;

	return S_OK;
}

void CBonTuner::UnloadTunerSignalStatistics(void)
{
	m_pIBDA_SignalStatisticsTunerNode.Release();
	m_pIBDA_SignalStatisticsDemodNode.Release();
	m_pIBDA_FrequencyFilter.Release();
	m_pIBDA_DigitalDemodulator.Release();
}

// Connect pins (Common subroutine)
//  Sﾄのピﾚ惰蝕
//
HRESULT CBonTuner::Connect(IBaseFilter* pFilterUp, IBaseFilter* pFilterDown)
{
	HRESULT hr;

	CDSEnumPins DSEnumPinsUp(pFilterUp);
	CDSEnumPins DSEnumPinsDown(pFilterDown);

	// 纓ｬtB^Outputsﾌ深v
	while (1) {
		CComPtr<IPin> pIPinUp;
		if (S_OK != (hr = DSEnumPinsUp.getNextPin(&pIPinUp, PIN_DIRECTION::PINDIR_OUTPUT))) {
			// [vI
			break;
		}
		do {
			CComPtr<IPin> pIPinPeerOfUp;
			// 纓ｬtB^ﾌ嬬ピﾚ双rG[邇淮上流s
			if (pIPinUp->ConnectedTo(&pIPinPeerOfUp) != VFW_E_NOT_CONNECTED){
				OutputDebug(L"  An already connected pin was found.\n");
				break;
			}
			// Pin name filtering check from INI (TargetPinName)
			{
				wchar_t szTargetPin[128] = {0};
				GetPrivateProfileString(L"Tuner", L"TargetPinName", L"", szTargetPin, 128, m_sIniFilePath.c_str());
				if (szTargetPin[0] != L'\0') {
					PIN_INFO pi = {0};
					pIPinUp->QueryPinInfo(&pi);
					if (pi.pFilter) pi.pFilter->Release();
					// If this is an output pin on a tuner/capture filter, check match
					if (wcsstr(pi.achName, L"Stream") != NULL) {
						if (wcscmp(pi.achName, szTargetPin) != 0) {
							OutputDebug(L"  Skipping pin [%s] (want [%s])\n", pi.achName, szTargetPin);
							break;
						}
					}
				}
			}

			// tB^Inputsﾌ深v
			DSEnumPinsDown.Reset();
			while (1) {
				CComPtr<IPin> pIPinDown;
				if (S_OK != (hr = DSEnumPinsDown.getNextPin(&pIPinDown, PIN_DIRECTION::PINDIR_INPUT))) {
					// [vI
					break;
				}
				do {
					CComPtr<IPin> pIPinPeerOfDown;
					// tB^ﾌ嬬ピﾚ双rG[邇淮鋭
					if (pIPinDown->ConnectedTo(&pIPinPeerOfDown) != VFW_E_NOT_CONNECTED) {
						OutputDebug(L"  An already connected pin was found.\n");
						break;
					}

					// ﾚ?
					PIN_INFO infoUp = {0}, infoDown = {0};
					pIPinUp->QueryPinInfo(&infoUp);
					if (infoUp.pFilter) infoUp.pFilter->Release();
					pIPinDown->QueryPinInfo(&infoDown);
					if (infoDown.pFilter) infoDown.pFilter->Release();
					if (SUCCEEDED(hr = m_pIGraphBuilder->ConnectDirect(pIPinUp, pIPinDown, NULL))) {
						AM_MEDIA_TYPE mt = {};
						if (SUCCEEDED(pIPinUp->ConnectionMediaType(&mt))) {
							LPOLESTR pszMaj = NULL, pszSub = NULL;
							StringFromCLSID(mt.majortype, &pszMaj);
							StringFromCLSID(mt.subtype, &pszSub);
							OutputDebug(L"  Connected Pin [%s] -> [%s] SUCCESS! (maj=%s sub=%s fixed=%d sampleSize=%lu cbFmt=%lu)\n",
								infoUp.achName, infoDown.achName,
								pszMaj ? pszMaj : L"?", pszSub ? pszSub : L"?",
								mt.bFixedSizeSamples, mt.lSampleSize, mt.cbFormat);
							if (pszMaj) CoTaskMemFree(pszMaj);
							if (pszSub) CoTaskMemFree(pszSub);
							if (mt.cbFormat && mt.pbFormat) CoTaskMemFree(mt.pbFormat);
							if (mt.pUnk) mt.pUnk->Release();
						} else {
							OutputDebug(L"  Connected Pin [%s] -> [%s] SUCCESS!\n", infoUp.achName, infoDown.achName);
						}
						return hr;
					} else {
						OutputDebug(L"  ConnectDirect [%s] -> [%s] failed hr=0x%08X\n", infoUp.achName, infoDown.achName, hr);
					}
				} while(0);
			} // while; ﾌ鋭
		} while (0);
	} // while ; ﾌ上流s

	// RlNgﾂ能ﾈ組ﾝ削せﾂ?
	OutputDebug(L"  Can not found a pair of connectable pins.\n");
	return E_FAIL;
}

void CBonTuner::DisconnectAll(IBaseFilter* pFilter)
{
	if (!m_pIGraphBuilder || !pFilter)
		return;
	
	HRESULT hr;

	CDSEnumPins DSEnumPins(pFilter);
	// sﾌ深v
	while (1) {
		CComPtr<IPin> pIPin;
		CComPtr<IPin> pIPinPeerOf;
		if (S_OK != (hr = DSEnumPins.getNextPin(&pIPin))) {
			// [vI
			break;
		}
		// sﾚ耐ぺ断
		if (SUCCEEDED(hr = pIPin->ConnectedTo(&pIPinPeerOf))) {
			hr = m_pIGraphBuilder->Disconnect(pIPinPeerOf);
			hr = m_pIGraphBuilder->Disconnect(pIPin);
		}
	}
}
