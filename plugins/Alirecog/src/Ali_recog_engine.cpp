/*
 * Copyright 2008-2015 Arsen Chaloyan
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * Mandatory rules concerning plugin implementation.
 * 1. Each plugin MUST implement a plugin/engine creator function
 *    with the exact signature and name (the main entry point)
 *        MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
 * 2. Each plugin MUST declare its version number
 *        MRCP_PLUGIN_VERSION_DECLARE
 * 3. One and only one response MUST be sent back to the received request.
 * 4. Methods (callbacks) of the MRCP engine channel MUST not block.
 *   (asynchronous response can be sent from the context of other thread)
 * 5. Methods (callbacks) of the MPF engine stream MUST not block.
 */

//#include "mrcp_recog_engine.h"
//#include "mpf_activity_detector.h"
//#include "apt_consumer_task.h"
//#include "apt_log.h"
//#include"Ali_recog_define.h"
#include"Ali_RecogEngine.h"
#include"Ali_RecogDefine.h"

#define RECOG_ENGINE_TASK_NAME "Ali Recog Engine"

using AliEngine = CRecogEngine<Ali_recog_channel_t>;
typedef struct Ali_recog_engine_t Ali_recog_engine_t;
typedef struct Ali_recog_channel_t Ali_recog_channel_t;
typedef struct Ali_recog_msg_t Ali_recog_msg_t;

/** Declaration of recognizer engine methods */
static apt_bool_t Ali_recog_engine_destroy(mrcp_engine_t *engine);
static apt_bool_t Ali_recog_engine_open(mrcp_engine_t *engine);
static apt_bool_t Ali_recog_engine_close(mrcp_engine_t *engine);
static apt_bool_t Ali_recog_recognition_complete(Ali_recog_channel_t *recog_channel, mrcp_recog_completion_cause_e cause);
static mrcp_engine_channel_t* Ali_recog_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool);

static const struct mrcp_engine_method_vtable_t engine_vtable = {
	Ali_recog_engine_destroy,
	Ali_recog_engine_open,
	Ali_recog_engine_close,
	Ali_recog_engine_channel_create
};


/** Declaration of recognizer channel methods */
static apt_bool_t Ali_recog_channel_destroy(mrcp_engine_channel_t *channel);
static apt_bool_t Ali_recog_channel_open(mrcp_engine_channel_t *channel);
static apt_bool_t Ali_recog_channel_close(mrcp_engine_channel_t *channel);
static apt_bool_t Ali_recog_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request);

static const struct mrcp_engine_channel_method_vtable_t channel_vtable = {
	Ali_recog_channel_destroy,
	Ali_recog_channel_open,
	Ali_recog_channel_close,
	Ali_recog_channel_request_process
};

/** Declaration of recognizer audio stream methods */
static apt_bool_t Ali_recog_stream_destroy(mpf_audio_stream_t *stream);
static apt_bool_t Ali_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec);
static apt_bool_t Ali_recog_stream_close(mpf_audio_stream_t *stream);
static apt_bool_t Ali_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame);

static const mpf_audio_stream_vtable_t audio_stream_vtable = {
	Ali_recog_stream_destroy,
	NULL,
	NULL,
	NULL,
	Ali_recog_stream_open,
	Ali_recog_stream_close,
	Ali_recog_stream_write,
	NULL
};



static apt_bool_t Ali_recog_msg_signal(Ali_recog_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request);
static apt_bool_t Ali_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg);

/** Declare this macro to set plugin version */
MRCP_PLUGIN_VERSION_DECLARE

/**
 * Declare this macro to use log routine of the server, plugin is loaded from.
 * Enable/add the corresponding entry in logger.xml to set a cutsom log source priority.
 *    <source name="RECOG-PLUGIN" priority="DEBUG" masking="NONE"/>
 */
MRCP_PLUGIN_LOG_SOURCE_IMPLEMENT(ALI_LOG_RECOG_PLUGIN,"ALI-RECOG-PLUGIN")

/** Create Ali recognizer engine */
MRCP_PLUGIN_DECLARE(mrcp_engine_t*) mrcp_plugin_create(apr_pool_t *pool)
{
	Ali_recog_engine_t *Ali_engine =(Ali_recog_engine_t *) apr_palloc(pool,sizeof(Ali_recog_engine_t));
	apt_task_t *task;
	apt_task_vtable_t *vtable;
	apt_task_msg_pool_t *msg_pool;

	msg_pool = apt_task_msg_pool_create_dynamic(sizeof(Ali_recog_msg_t),pool);
	Ali_engine->task = apt_consumer_task_create(Ali_engine,msg_pool,pool);
	if(!Ali_engine->task) {
		return NULL;
	}
	task = apt_consumer_task_base_get(Ali_engine->task);
	apt_task_name_set(task,RECOG_ENGINE_TASK_NAME);
	vtable = apt_task_vtable_get(task);
	if(vtable) {
		vtable->process_msg = Ali_recog_msg_process;
	}

	/* create engine base */
	return mrcp_engine_create(
				MRCP_RECOGNIZER_RESOURCE,  /* MRCP resource identifier */
				Ali_engine,               /* object to associate */
				&engine_vtable,            /* virtual methods table of engine */
				pool);                     /* pool to allocate memory from */
}

/** Destroy recognizer engine */
static apt_bool_t Ali_recog_engine_destroy(mrcp_engine_t *engine)
{
	Ali_recog_engine_t *Ali_engine =(Ali_recog_engine_t *) engine->obj;
	if(Ali_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(Ali_engine->task);
		apt_task_destroy(task);
		Ali_engine->task = NULL;
	}
	return TRUE;
}

/** Open recognizer engine */
static apt_bool_t Ali_recog_engine_open(mrcp_engine_t *engine)
{
	Ali_recog_engine_t *Ali_engine = (Ali_recog_engine_t *)engine->obj;
	if(Ali_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(Ali_engine->task);
		apt_task_start(task);
	}

	AliEngine::recogEngine = CAliRecogEngine::GetAliRecogEngine();
	if (AliEngine::recogEngine) {
		if (!AliEngine::recogEngine->EngineInit(engine)) {
			LOG_ERROR("<---------- Engine Init Failed ---------->")
		}
		//LOG_INFO()
	}

	return mrcp_engine_open_respond(engine,TRUE);
}

/** Close recognizer engine */
static apt_bool_t Ali_recog_engine_close(mrcp_engine_t *engine)
{
	Ali_recog_engine_t *Ali_engine = (Ali_recog_engine_t *)engine->obj;
	if(Ali_engine->task) {
		apt_task_t *task = apt_consumer_task_base_get(Ali_engine->task);
		apt_task_terminate(task,TRUE);
	}

	if (AliEngine::recogEngine) {
		AliEngine::recogEngine->EngineUinit();
		LOG_INFO("<----------- Engine uninit success ----------> ")
	}

	return mrcp_engine_close_respond(engine);
}

static mrcp_engine_channel_t* Ali_recog_engine_channel_create(mrcp_engine_t *engine, apr_pool_t *pool)
{
	mpf_stream_capabilities_t *capabilities;
	mpf_termination_t *termination; 

	/* create Ali recog channel */
	Ali_recog_channel_t *recog_channel = (Ali_recog_channel_t *)apr_palloc(pool,sizeof(Ali_recog_channel_t));
	recog_channel->Ali_engine = (Ali_recog_engine_t *)engine->obj;
	recog_channel->recog_request = NULL;
	recog_channel->stop_response = NULL;
	recog_channel->detector = mpf_activity_detector_create(pool);
	recog_channel->audio_out = NULL;

	capabilities = mpf_sink_stream_capabilities_create(pool);
	mpf_codec_capabilities_add(
			&capabilities->codecs,
			MPF_SAMPLE_RATE_8000 | MPF_SAMPLE_RATE_16000,
			"LPCM");

	/* create media termination */
	termination = mrcp_engine_audio_termination_create(
			recog_channel,        /* object to associate */
			&audio_stream_vtable, /* virtual methods table of audio stream */
			capabilities,         /* stream capabilities */
			pool);                /* pool to allocate memory from */

	/* create engine channel base */
	recog_channel->channel = mrcp_engine_channel_create(
			engine,               /* engine */
			&channel_vtable,      /* virtual methods table of engine channel */
			recog_channel,        /* object to associate */
			termination,          /* associated media termination */
			pool);                /* pool to allocate memory from */

	apt_log(ALI_RECOG_LOG_MARK, APT_PRIO_INFO, "<----- recog_channel create ----->");
	return recog_channel->channel;
}

/** Destroy engine channel */
static apt_bool_t Ali_recog_channel_destroy(mrcp_engine_channel_t *channel)
{
	/* nothing to destrtoy */
	apt_log(ALI_RECOG_LOG_MARK, APT_PRIO_INFO, "<----- recog_channel Destroy ----->");
	return TRUE;
}

/** Open engine channel (asynchronous response MUST be sent)*/
static apt_bool_t Ali_recog_channel_open(mrcp_engine_channel_t *channel)
{
	apt_log(ALI_RECOG_LOG_MARK, APT_PRIO_INFO, "<----- Ali_recog_channel Open  ----->");
	return Ali_recog_msg_signal(Ali_RECOG_MSG_OPEN_CHANNEL,channel,NULL);
}

/** Close engine channel (asynchronous response MUST be sent)*/
static apt_bool_t Ali_recog_channel_close(mrcp_engine_channel_t *channel)
{
	apt_log(ALI_RECOG_LOG_MARK, APT_PRIO_INFO, "<----- Ali_recog_channel Close  ----->");
	return Ali_recog_msg_signal(Ali_RECOG_MSG_CLOSE_CHANNEL,channel,NULL);
}

/** Process MRCP channel request (asynchronous response MUST be sent)*/
static apt_bool_t Ali_recog_channel_request_process(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{

	return Ali_recog_msg_signal(Ali_RECOG_MSG_REQUEST_PROCESS,channel,request);
}

/** Process RECOGNIZE request */
static apt_bool_t Ali_recog_channel_recognize(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	/* process RECOGNIZE request */
	mrcp_recog_header_t *recog_header;
	Ali_recog_channel_t *recog_channel = (Ali_recog_channel_t *)channel->method_obj;
	const mpf_codec_descriptor_t *descriptor = mrcp_engine_sink_stream_codec_get(channel);

	if(!descriptor) {
		apt_log(ALI_RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to Get Codec Descriptor " APT_SIDRES_FMT, MRCP_MESSAGE_SIDRES(request));
		response->start_line.status_code = MRCP_STATUS_CODE_METHOD_FAILED;
		return FALSE;
	}

	recog_channel->timers_started = TRUE;

	/* get recognizer header */
	recog_header =(mrcp_recog_header_t *) mrcp_resource_header_get(request);
	if(recog_header) {
		if(mrcp_resource_header_property_check(request,RECOGNIZER_HEADER_START_INPUT_TIMERS) == TRUE) {
			recog_channel->timers_started = recog_header->start_input_timers;
		}
		if(mrcp_resource_header_property_check(request,RECOGNIZER_HEADER_NO_INPUT_TIMEOUT) == TRUE) {
			mpf_activity_detector_noinput_timeout_set(recog_channel->detector,recog_header->no_input_timeout);
		}
		if(mrcp_resource_header_property_check(request,RECOGNIZER_HEADER_SPEECH_COMPLETE_TIMEOUT) == TRUE) {
			mpf_activity_detector_silence_timeout_set(recog_channel->detector,recog_header->speech_complete_timeout);
		}
	}

	//if(!recog_channel->audio_out) {
	//	const apt_dir_layout_t *dir_layout = channel->engine->dir_layout;
	//	char *file_name = apr_psprintf(channel->pool,"utter-%dkHz-%s.pcm",
	//						descriptor->sampling_rate/1000,
	//						request->channel_id.session_id.buf);
	//	char *file_path = apt_vardir_filepath_get(dir_layout,file_name,channel->pool);
	//	if(file_path) {
	//		apt_log(RECOG_LOG_MARK,APT_PRIO_INFO,"Open Utterance Output File [%s] for Writing",file_path);
	//		recog_channel->audio_out = fopen(file_path,"wb");
	//		if(!recog_channel->audio_out) {
	//			apt_log(RECOG_LOG_MARK,APT_PRIO_WARNING,"Failed to Open Utterance Output File [%s] for Writing",file_path);
	//		}
	//	}
	//}

	if (AliEngine::recogEngine) {
		if (!AliEngine::recogEngine->EngineReocgStart(recog_channel)) {
			LOG_ERROR("Engine Recog Start Failed ! ! !");
		}
	}

	apt_log(ALI_RECOG_LOG_MARK, APT_PRIO_INFO, "<----- Process RECOGNIZE request ----->");

	response->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
	/* send asynchronous response */
	mrcp_engine_channel_message_send(channel,response);
	recog_channel->recog_request = request;
	return TRUE;
}

/** Process STOP request */
static apt_bool_t Ali_recog_channel_stop(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	/* process STOP request */
	Ali_recog_channel_t *recog_channel = (Ali_recog_channel_t *)channel->method_obj;
	/* store STOP request, make sure there is no more activity and only then send the response */
	recog_channel->stop_response = response;
	apt_log(ALI_RECOG_LOG_MARK, APT_PRIO_INFO, "<----- Process STOP request ----->");
	return TRUE;
}

/** Process START-INPUT-TIMERS request */
static apt_bool_t Ali_recog_channel_timers_start(mrcp_engine_channel_t *channel, mrcp_message_t *request, mrcp_message_t *response)
{
	Ali_recog_channel_t *recog_channel = (Ali_recog_channel_t *)channel->method_obj;
	recog_channel->timers_started = TRUE;
	return mrcp_engine_channel_message_send(channel,response);
}

/** Dispatch MRCP request */
static apt_bool_t Ali_recog_channel_request_dispatch(mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	apt_bool_t processed = FALSE;
	mrcp_message_t *response = mrcp_response_create(request,request->pool);
	switch(request->start_line.method_id) {
		case RECOGNIZER_SET_PARAMS:
			break;
		case RECOGNIZER_GET_PARAMS:
			break;
		case RECOGNIZER_DEFINE_GRAMMAR:
			break;
		case RECOGNIZER_RECOGNIZE:
			processed = Ali_recog_channel_recognize(channel,request,response);
			break;
		case RECOGNIZER_GET_RESULT:
			break;
		case RECOGNIZER_START_INPUT_TIMERS:
			processed = Ali_recog_channel_timers_start(channel,request,response);
			break;
		case RECOGNIZER_STOP:
			processed = Ali_recog_channel_stop(channel,request,response);
			break;
		default:
			break;
	}
	if(processed == FALSE) {
		/* send asynchronous response for not handled request */
		mrcp_engine_channel_message_send(channel,response);
	}
	return TRUE;
}

/** Callback is called from MPF engine context to destroy any additional data associated with audio stream */
static apt_bool_t Ali_recog_stream_destroy(mpf_audio_stream_t *stream)
{
	LOG_INFO("<---------------- recog stream  destroy ------------------>")
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action before open */
static apt_bool_t Ali_recog_stream_open(mpf_audio_stream_t *stream, mpf_codec_t *codec)
{
	LOG_INFO("<---------------- recog stream  open ------------------>")
	return TRUE;
}

/** Callback is called from MPF engine context to perform any action after close */
static apt_bool_t Ali_recog_stream_close(mpf_audio_stream_t *stream)
{
	LOG_INFO("<---------------- recog stream  close ------------------>")
	Ali_recog_channel_t *recog_channel = (Ali_recog_channel_t *)stream->obj;
	if (recog_channel) {
		if (AliEngine::recogEngine) {
			AliEngine::recogEngine->EngineReocgStop(recog_channel);
		}
	}
	return TRUE;
}

/* Raise Ali START-OF-INPUT event */
static apt_bool_t Ali_recog_start_of_input(Ali_recog_channel_t *recog_channel)
{
	/* create START-OF-INPUT event */
	mrcp_message_t *message = mrcp_event_create(
						recog_channel->recog_request,
						RECOGNIZER_START_OF_INPUT,
						recog_channel->recog_request->pool);
	if(!message) {
		return FALSE;
	}

	/* set request state */
	message->start_line.request_state = MRCP_REQUEST_STATE_INPROGRESS;
	/* send asynch event */
	return mrcp_engine_channel_message_send(recog_channel->channel,message);
}

/* Load Ali recognition result */
static apt_bool_t Ali_recog_result_load(Ali_recog_channel_t *recog_channel, mrcp_message_t *message)
{
	FILE *file;
	mrcp_engine_channel_t *channel = recog_channel->channel;
	const apt_dir_layout_t *dir_layout = channel->engine->dir_layout;
	char *file_path = apt_datadir_filepath_get(dir_layout,"result.xml",message->pool);
	if(!file_path) {
		return FALSE;
	}
	
	/* read the Ali result from file */
	file = fopen(file_path,"r");
	if(file) {
		mrcp_generic_header_t *generic_header;
		char text[1024];
		apr_size_t size;
		size = fread(text,1,sizeof(text),file);
		apt_string_assign_n(&message->body,text,size,message->pool);
		fclose(file);

		/* get/allocate generic header */
		generic_header = mrcp_generic_header_prepare(message);
		if(generic_header) {
			/* set content types */
			apt_string_assign(&generic_header->content_type,"application/x-nlsml",message->pool);
			mrcp_generic_header_property_add(message,GENERIC_HEADER_CONTENT_TYPE);
		}
	}
	return TRUE;
}

/* Raise Ali RECOGNITION-COMPLETE event */
static apt_bool_t Ali_recog_recognition_complete(Ali_recog_channel_t *recog_channel, mrcp_recog_completion_cause_e cause)
{
	mrcp_recog_header_t *recog_header;
	/* create RECOGNITION-COMPLETE event */
	mrcp_message_t *message = mrcp_event_create(
						recog_channel->recog_request,
						RECOGNIZER_RECOGNITION_COMPLETE,
						recog_channel->recog_request->pool);
	if(!message) {
		return FALSE;
	}

	/* get/allocate recognizer header */
	recog_header =(mrcp_recog_header_t *) mrcp_resource_header_prepare(message);
	if(recog_header) {
		/* set completion cause */
		recog_header->completion_cause = cause;
		mrcp_resource_header_property_add(message, RECOGNIZER_HEADER_COMPLETION_CAUSE);
	}
	/* set request state */
	message->start_line.request_state = MRCP_REQUEST_STATE_COMPLETE;

	if(cause == RECOGNIZER_COMPLETION_CAUSE_SUCCESS) {
		//Ali_recog_result_load(recog_channel,message);
	}

	recog_channel->recog_request = NULL;
	/* send asynch event */
	return mrcp_engine_channel_message_send(recog_channel->channel,message);
}

/** Callback is called from MPF engine context to write/send new frame */
static apt_bool_t Ali_recog_stream_write(mpf_audio_stream_t *stream, const mpf_frame_t *frame)
{
	Ali_recog_channel_t *recog_channel = (Ali_recog_channel_t *)stream->obj;
	if(recog_channel->stop_response) {
		/* send asynchronous response to STOP request */
		mrcp_engine_channel_message_send(recog_channel->channel,recog_channel->stop_response);
		recog_channel->stop_response = NULL;
		recog_channel->recog_request = NULL;
		//RECOGNIZER_STATE_IDLE
		return TRUE;
	}


	if(recog_channel->recog_request) {

		mpf_detector_event_e det_event = mpf_activity_detector_process(recog_channel->detector,frame);
		switch(det_event) {
			case MPF_DETECTOR_EVENT_ACTIVITY:
				apt_log(ALI_RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Voice Activity " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				Ali_recog_start_of_input(recog_channel);
				break;
			case MPF_DETECTOR_EVENT_INACTIVITY:
				apt_log(ALI_RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Voice Inactivity " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				Ali_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
				break;
			case MPF_DETECTOR_EVENT_NOINPUT:
				apt_log(ALI_RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Noinput " APT_SIDRES_FMT,
					MRCP_MESSAGE_SIDRES(recog_channel->recog_request));
				if(recog_channel->timers_started == TRUE) {
					Ali_recog_recognition_complete(recog_channel, RECOGNIZER_COMPLETION_CAUSE_NO_INPUT_TIMEOUT);
				}
				break;
			default:
				break;
		}

		if(recog_channel->recog_request) {
			if((frame->type & MEDIA_FRAME_TYPE_EVENT) == MEDIA_FRAME_TYPE_EVENT) {
				if(frame->marker == MPF_MARKER_START_OF_EVENT) {
					apt_log(ALI_RECOG_LOG_MARK,APT_PRIO_INFO,"Detected Start of Event " APT_SIDRES_FMT " id:%d",
						MRCP_MESSAGE_SIDRES(recog_channel->recog_request),
						frame->event_frame.event_id);
				}
				else if(frame->marker == MPF_MARKER_END_OF_EVENT) {
					apt_log(ALI_RECOG_LOG_MARK,APT_PRIO_INFO,"Detected End of Event " APT_SIDRES_FMT " id:%d duration:%d ts",
						MRCP_MESSAGE_SIDRES(recog_channel->recog_request),
						frame->event_frame.event_id,
						frame->event_frame.duration);
				}
			}
		}

		//if(recog_channel->audio_out) {
		//	fwrite(frame->codec_frame.buffer,1,frame->codec_frame.size,recog_channel->audio_out);
		//}
		
		if (AliEngine::recogEngine) {
			AliEngine::recogEngine->EngineWriteFrame(recog_channel, frame);
			string reslut = AliEngine::recogEngine->EngineRecogCompleted(recog_channel);
			
			if (reslut.length() != 0 && recog_channel->recog_request)
			{
				mrcp_method_id event_id;
				mrcp_request_state_e event_stat;

				if (AliEngine::recogEngine->EngineRecogModule() == RECOGNIZER) {
					event_id = RECOGNIZER_RECOGNITION_COMPLETE;
					event_stat = MRCP_REQUEST_STATE_COMPLETE;
				}
				else {
					event_id = RECOGNIZER_INTERMEDIATE_COMPLETE;
					event_stat = MRCP_REQUEST_STATE_INPROGRESS;
				}

				Json::Value root;
				Json::FastWriter write;

				root["result"] = reslut;
				string Result = write.write(root);

				//创建一个mrcp事件
				mrcp_recog_header_t *recog_header;
				mrcp_message_t *message = mrcp_event_create(recog_channel->recog_request,event_id,recog_channel->recog_request->pool);
				if (!message) {
					LOG_ERROR("<--------- create mrcp message failed ----------->");
					return TRUE;
				}

				//创建资源头
				recog_header = (mrcp_recog_header_t *)mrcp_resource_header_prepare(message);
				if (recog_header) {
					//先事件头添加完成码
					recog_header->completion_cause = RECOGNIZER_COMPLETION_CAUSE_SUCCESS;
					mrcp_resource_header_property_add(message, RECOGNIZER_COMPLETION_CAUSE_SUCCESS);
				}
				//添加事件状态
				message->start_line.request_state = event_stat;

				//添加结果
				apt_string_assign_n(&message->body, Result.c_str(), Result.length(), message->pool);

				//添加结果的格式
				mrcp_generic_header_t *generic_header = mrcp_generic_header_prepare(message);
				if (generic_header) {
					apt_string_assign(&generic_header->content_type, "application/json", message->pool);
					mrcp_generic_header_property_add(message, GENERIC_HEADER_CONTENT_TYPE);
				}
				//发送异步事件
				LOG_INFO("<----------- Send MRCP Event ------------->");
				mrcp_engine_channel_message_send(recog_channel->channel, message);
			}
		}
	}
	return TRUE;
}

static apt_bool_t Ali_recog_msg_signal(Ali_recog_msg_type_e type, mrcp_engine_channel_t *channel, mrcp_message_t *request)
{
	apt_bool_t status = FALSE;
	Ali_recog_channel_t *Ali_channel = (Ali_recog_channel_t *)channel->method_obj;
	Ali_recog_engine_t *Ali_engine = Ali_channel->Ali_engine;
	apt_task_t *task = apt_consumer_task_base_get(Ali_engine->task);
	apt_task_msg_t *msg = apt_task_msg_get(task);
	if(msg) {
		Ali_recog_msg_t *Ali_msg;
		msg->type = TASK_MSG_USER;
		Ali_msg = (Ali_recog_msg_t*) msg->data;

		Ali_msg->type = type;
		Ali_msg->channel = channel;
		Ali_msg->request = request;
		status = apt_task_msg_signal(task,msg);
	}
	return status;
}

static apt_bool_t Ali_recog_msg_process(apt_task_t *task, apt_task_msg_t *msg)
{
	Ali_recog_msg_t *Ali_msg = (Ali_recog_msg_t*)msg->data;
	switch(Ali_msg->type) {
		case Ali_RECOG_MSG_OPEN_CHANNEL:
			/* open channel and send asynch response */
			mrcp_engine_channel_open_respond(Ali_msg->channel,TRUE);
			break;
		case Ali_RECOG_MSG_CLOSE_CHANNEL:
		{
			/* close channel, make sure there is no activity and send asynch response */
			Ali_recog_channel_t *recog_channel = (Ali_recog_channel_t *)Ali_msg->channel->method_obj;
			if(recog_channel->audio_out) {
				fclose(recog_channel->audio_out);
				recog_channel->audio_out = NULL;
			}

			mrcp_engine_channel_close_respond(Ali_msg->channel);
			break;
		}
		case Ali_RECOG_MSG_REQUEST_PROCESS:
			Ali_recog_channel_request_dispatch(Ali_msg->channel,Ali_msg->request);
			break;
		default:
			break;
	}
	return TRUE;
}
