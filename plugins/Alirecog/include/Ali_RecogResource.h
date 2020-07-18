#ifndef __ALI_RECOG_RESOURCE_H__
#define __ALI_RECOG_RESOURCE_H__

#include<string>
#include<queue>
#include"nlsClient.h"
#include"nlsEvent.h"
#include"Ali_RecogDefine.h"


#if _WIN32
#include "nlsCommonSdk\Token.h"
#include"json\json.h"
#else
#include "nlsCommonSdk/Token.h"
#include "json/json.h"
#endif

using std::string;
using std::queue;
using AlibabaNls::NlsClient;
using AlibabaNls::NlsEvent;
using AlibabaNlsCommon::NlsToken;

using resultQueue = queue<string>;

class CAliChannel;

class CAliResource 
{
public:
	CAliResource()noexcept;
	virtual ~CAliResource()noexcept;
private:
	CAliResource(CAliResource&);
	CAliResource& operator=(CAliResource&);

public:
	virtual bool			DoRecogRequestInit( const string& appKey,
												const string& AccessKey,
												const string& Secret,
												const string& Format,
												const string& SampleRate,
												const string& intermediateResult,
												const string& inverseText,
												const string& voiceDetection,
												const string& maxEndSilence,
												const string& maxStartSilence,
												const string& PunctuationPrediction,
												const string& CustomizationId,
												const string& VocabularyId,
												const string& OutputFormat,
												const string& ContextParam,
												apr_pool_t *  pool) = 0;
	virtual bool			DoCreateRecogRequest() = 0;
	virtual void			DoDestroyRecogRequest() = 0;
	virtual const string	DoRecogResultGet() = 0;
	virtual int				DoWriteFrame(const mpf_frame_t *frame) = 0;

public:
	//ʵʱʶ��ص�
	 static void	OnTranscriptionStarted(NlsEvent* ev, void* AliRe);
	 static void	OnTranscriptionResultChanged(NlsEvent* ev, void* AliRe);
	 static void	OnTranscriptionCompleted(NlsEvent* ev, void* AliRe);
	 static void	OnSentenceBegin(NlsEvent* ev, void* AliRe);
	 static void	OnSentenceEnd(NlsEvent* ev, void* AliRe);

	//һ�仰�ص�
	 static void	OnRecognitionCompleted(NlsEvent* ev, void* AliRe);
	 static void	OnRecognitionStarted(NlsEvent* ev, void* AliRe);
	 static void	OnRecognitionResultChanged(NlsEvent* ev, void* AliRe);

	 static void	OnTaskFailed(NlsEvent* ev, void* AliRe);
	 static void	OnChannelClosed(NlsEvent* ev, void* AliRe);

protected:
	virtual void	OnRecognitionCompleted(NlsEvent* ev) { };
	virtual void	OnRecognitionStarted(NlsEvent* ev) { };
	virtual void	OnRecognitionResultChanged(NlsEvent* ev) { };
	virtual void	OnTranscriptionStarted(NlsEvent* ev) { };
	virtual void	OnTranscriptionResultChanged(NlsEvent* ev) { };
	virtual void	OnTranscriptionCompleted(NlsEvent* ev) { };
	virtual void	OnSentenceBegin(NlsEvent* ev) { };
	virtual void	OnSentenceEnd(NlsEvent* ev) { };

	virtual void	OnTaskFailed(NlsEvent* ev) = 0;
	virtual void	OnChannelClosed(NlsEvent* ev) = 0;

protected:
	int				DoAliToKenGet(string& AccessKeyID, string& AccessKeySecret);
	void			DoCleanAliPram();
public:
	bool			DoCheakRecogToKen();

protected:
	unsigned long		m_expireTime;
	string				m_appkey;
	string				m_AccessKey;
	string				m_Secret;
	string				m_Format;
	string				m_SampleRate;
	string				m_intermediateResult;//�м���
	string				m_inverseText;//
	string				m_voiceDetection;
	string				m_maxEndSilence;
	string				m_maxStartSilence;
	string				m_PunctuationPrediction;
	string				m_CustomizationId;
	string				m_VocabularyId;
	string				m_OutputFormat;
	string				m_ContextParam;
	string				m_token;
	//CAliChannel*		m_AliCh;
	resultQueue			m_resultQueue;
	apr_pool_t *		m_pool;
	apr_thread_mutex_t*	m_lock;
};



#endif // !__ALI_RECOG_RESOURCE_H__

