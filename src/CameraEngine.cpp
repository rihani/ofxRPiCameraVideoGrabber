#include "CameraEngine.h"


CameraEngine::CameraEngine()
{
	didOpen		= false;
	
	didWriteFile = false;
	
	numMBps = 2.0;
	
	stopRequested = false;	 
	isStopping = false;
	isKeyframeValid = false;
	doFillBuffer = false;
	bufferAvailable = false;
	
	recordedFrameCounter = 0;
    renderedFrameCounter = 0;
    isCurrentlyRecording = false;
    encoder = NULL;
    splitter = NULL;
    render = NULL;
    camera = NULL;
    
    eglBuffer	= NULL;
    eglImage = NULL;
    sessionConfig = NULL;
    cameraOutputPort = CAMERA_OUTPUT_PORT;
    //cameraOutputPort = CAMERA_PREVIEW_PORT;

}   


int CameraEngine::getFrameCounter()
{
    return renderedFrameCounter;
    if(!didOpen) return 0;
    
    if(engineType == TEXTURE_ENGINE)
    {
        return renderedFrameCounter;
    }
#if 0
    //used to work?
    OMX_CONFIG_BRCMCAMERASTATSTYPE stats;
    OMX_INIT_STRUCTURE(stats);
    //stats.nPortIndex = CAMERA_OUTPUT_PORT;
    OMX_ERRORTYPE error =OMX_GetParameter(render, OMX_IndexConfigBrcmCameraStats, &stats);
    OMX_TRACE(error);
    if (error == OMX_ErrorNone)
    {
        /*OMX_U32 nImageCount;
         OMX_U32 nBufferCount;
         OMX_U32 nFrameCount;
         OMX_U32 nFrameSkips;
         OMX_U32 nDiscards;
         OMX_U32 nEOS;
         OMX_U32 nMaxFrameSize;
         
         OMX_TICKS nByteCount;
         OMX_TICKS nMaxTimeDelta;
         OMX_U32 nCorruptMBs;*/
        //ofLogVerbose(__func__) << "nFrameCount: " << stats.nFrameCount;
        return stats.nOutFrameCount;
    }
#endif
    return 0;
}


void CameraEngine::setup(SessionConfig* sessionConfig_)
{
    sessionConfig = sessionConfig_;
    
    if(sessionConfig->isUsingTexture())
    {
        engineType = TEXTURE_ENGINE;
        engineTypeString = OMX_EGL_RENDER;
        renderInputPort = EGL_RENDER_INPUT_PORT;
    }else
    {
        engineType = NON_TEXTURE_ENGINE;
        engineTypeString = OMX_VIDEO_RENDER;
        renderInputPort = VIDEO_RENDER_INPUT_PORT;
    }
    OMX_ERRORTYPE error = OMX_ErrorNone;
    
    OMX_CALLBACKTYPE cameraCallbacks;
    
    cameraCallbacks.EventHandler    = &CameraEngine::cameraEventHandlerCallback;
    cameraCallbacks.EmptyBufferDone	= &CameraEngine::nullEmptyBufferDone;
    cameraCallbacks.FillBufferDone	= &CameraEngine::nullFillBufferDone;
    
    error = OMX_GetHandle(&camera, OMX_CAMERA, this , &cameraCallbacks);
    if(error != OMX_ErrorNone) 
    {
        OMX_TRACE(error, "camera OMX_GetHandle FAIL");
    }
    
    configureCameraResolution();
    
}

inline
OMX_ERRORTYPE CameraEngine::egl_renderFillBufferDone(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{	
    CameraEngine *grabber = static_cast<CameraEngine*>(pAppData);
    grabber->renderedFrameCounter++;
    OMX_ERRORTYPE error = OMX_FillThisBuffer(hComponent, pBuffer);
    return error;
}

inline
OMX_ERRORTYPE CameraEngine::encoderFillBufferDone(OMX_HANDLETYPE hComponent,
                                                OMX_PTR pAppData,
                                                OMX_BUFFERHEADERTYPE* pBuffer)
{	
    CameraEngine *grabber = static_cast<CameraEngine*>(pAppData);
    grabber->lock();
    //ofLogVerbose(__func__) << "recordedFrameCounter: " << grabber->recordedFrameCounter;
    grabber->bufferAvailable = true;
    grabber->recordedFrameCounter++;
    grabber->unlock();
    return OMX_ErrorNone;
}

inline
OMX_ERRORTYPE CameraEngine::cameraEventHandlerCallback(OMX_HANDLETYPE hComponent,
                                                     OMX_PTR pAppData,
                                                     OMX_EVENTTYPE eEvent,
                                                     OMX_U32 nData1,
                                                     OMX_U32 nData2,
                                                     OMX_PTR pEventData)
{
    /*ofLog(OF_LOG_VERBOSE, 
     "TextureEngine::%s - eEvent(0x%x), nData1(0x%lx), nData2(0x%lx), pEventData(0x%p)\n",
     __func__, eEvent, nData1, nData2, pEventData);*/
    CameraEngine *grabber = static_cast<CameraEngine*>(pAppData);
    //ofLogVerbose(__func__) << OMX_Maps::getInstance().eventTypes[eEvent];
    switch (eEvent) 
    {
        case OMX_EventParamOrConfigChanged:
        {
            
            return grabber->onCameraEventParamOrConfigChanged();
        }	
            
        case OMX_EventError:
        {
            OMX_TRACE((OMX_ERRORTYPE)nData1);
        }
        default: 
        {
            /*ofLog(OF_LOG_VERBOSE, 
             "TextureEngine::%s - eEvent(0x%x), nData1(0x%lx), nData2(0x%lx), pEventData(0x%p)\n",
             __func__, eEvent, nData1, nData2, pEventData);*/
            
            break;
        }
    }
    return OMX_ErrorNone;
}

inline
OMX_ERRORTYPE CameraEngine::configureCameraResolution()
{
    
    OMX_ERRORTYPE error;
    error = DisableAllPortsForComponent(&camera);
	OMX_TRACE(error);
    
    
  
    
    OMX_PARAM_TIMESTAMPMODETYPE timestampConfig;
    OMX_INIT_STRUCTURE(timestampConfig);
    timestampConfig.eTimestampMode = OMX_TimestampModeResetStc;
    error =  OMX_SetParameter(camera, OMX_IndexParamCommonUseStcTimestamps, &timestampConfig);
    OMX_TRACE(error);
    
	OMX_CONFIG_REQUESTCALLBACKTYPE cameraCallback;
	OMX_INIT_STRUCTURE(cameraCallback);
	cameraCallback.nPortIndex	=	OMX_ALL;
	cameraCallback.nIndex		=	OMX_IndexParamCameraDeviceNumber;
	cameraCallback.bEnable		=	OMX_TRUE;
	
	error = OMX_SetConfig(camera, OMX_IndexConfigRequestCallback, &cameraCallback);
	OMX_TRACE(error);
    
	//Set the camera (always 0)
	OMX_PARAM_U32TYPE device;
	OMX_INIT_STRUCTURE(device);
	device.nPortIndex	= OMX_ALL;
	device.nU32			= 0;
	
	error = OMX_SetParameter(camera, OMX_IndexParamCameraDeviceNumber, &device);
	OMX_TRACE(error);
    
	//Set the resolution
	OMX_PARAM_PORTDEFINITIONTYPE cameraOutputPortDefinition;
	OMX_INIT_STRUCTURE(cameraOutputPortDefinition);
	cameraOutputPortDefinition.nPortIndex = cameraOutputPort;
	
	error =  OMX_GetParameter(camera, OMX_IndexParamPortDefinition, &cameraOutputPortDefinition);
    OMX_TRACE(error);
	if(error == OMX_ErrorNone) 
	{
        cameraOutputPortDefinition.format.video.nFrameWidth		= sessionConfig->width;
        cameraOutputPortDefinition.format.video.nFrameHeight	= sessionConfig->height;
        cameraOutputPortDefinition.format.video.xFramerate		= sessionConfig->framerate << 16;

        cameraOutputPortDefinition.format.video.nStride			= sessionConfig->width;
        //below works but leaving it at default 0
        //cameraOutputPortDefinition.format.video.nSliceHeight	= round(sessionConfig->height / 16) * 16;
        
        error =  OMX_SetParameter(camera, OMX_IndexParamPortDefinition, &cameraOutputPortDefinition);
        OMX_TRACE(error);
	}
    
    
        
#if 1    
    error =  OMX_GetParameter(camera, OMX_IndexParamPortDefinition, &cameraOutputPortDefinition);
    OMX_TRACE(error);
    ofLogVerbose(__func__) <<   "Compression Format: "  << OMX_Maps::getInstance().getVideoCoding(cameraOutputPortDefinition.format.video.eCompressionFormat);
    ofLogVerbose(__func__) <<   "Color Format: "        << OMX_Maps::getInstance().getColorFormat(cameraOutputPortDefinition.format.video.eColorFormat);
#endif    
    return error;
}

//OMX_COLOR_FormatBRCMOpaque

inline
OMX_ERRORTYPE CameraEngine::configureEncoder()
{
		
    OMX_ERRORTYPE error;
    
    error = DisableAllPortsForComponent(&encoder);
    OMX_TRACE(error);

	// Encoder input port definition is done automatically upon tunneling
	
	// Configure video format emitted by encoder output port
	OMX_PARAM_PORTDEFINITIONTYPE encoderOutputPortDefinition;
	OMX_INIT_STRUCTURE(encoderOutputPortDefinition);
	encoderOutputPortDefinition.nPortIndex = ENCODER_OUTPUT_PORT;
	error =OMX_GetParameter(encoder, OMX_IndexParamPortDefinition, &encoderOutputPortDefinition);
    OMX_TRACE(error);
    //OMX_VIDEO_AVCProfileHigh
    //OMX_VIDEO_CodingAVC
	if (error == OMX_ErrorNone) 
	{
        
        stringstream encoderPortInfo;
        encoderPortInfo << "ENCODER PORT INFO:\n";
        encoderPortInfo << "nBufferCountMin: "       << encoderOutputPortDefinition.nBufferCountMin      << "\n";
        encoderPortInfo << "nBufferCountActual: "    << encoderOutputPortDefinition.nBufferCountActual   << "\n";
        encoderPortInfo << "nBufferSize: "           << encoderOutputPortDefinition.nBufferSize          << "\n";
        encoderPortInfo << "nBufferAlignment: "      << encoderOutputPortDefinition.nBufferAlignment     << "\n";
        encoderPortInfo << "Compression Format: "    << OMX_Maps::getInstance().getVideoCoding(encoderOutputPortDefinition.format.video.eCompressionFormat) << "\n";

        encoderPortInfo << "Color Format: " << OMX_Maps::getInstance().getColorFormat(encoderOutputPortDefinition.format.video.eColorFormat) << "\n";
         
        ofLogVerbose(__func__) << encoderPortInfo.str();
        
	}
	
	
	
	//colorFormatTypes
	recordingBitRate = MEGABYTE_IN_BITS * numMBps;
	encoderOutputPortDefinition.format.video.nBitrate = recordingBitRate;
	error = OMX_SetParameter(encoder, OMX_IndexParamPortDefinition, &encoderOutputPortDefinition);
    OMX_TRACE(error);

	// Configure encoding bitrate
	OMX_VIDEO_PARAM_BITRATETYPE encodingBitrate;
	OMX_INIT_STRUCTURE(encodingBitrate);
	encodingBitrate.eControlRate = OMX_Video_ControlRateVariable;
	//encodingBitrate.eControlRate = OMX_Video_ControlRateConstant;
	
	encodingBitrate.nTargetBitrate = recordingBitRate;
	encodingBitrate.nPortIndex = ENCODER_OUTPUT_PORT;
	
	error = OMX_SetParameter(encoder, OMX_IndexParamVideoBitrate, &encodingBitrate);
    OMX_TRACE(error);

	// Configure encoding format
	OMX_VIDEO_PARAM_PORTFORMATTYPE encodingFormat;
	OMX_INIT_STRUCTURE(encodingFormat);
	encodingFormat.nPortIndex = ENCODER_OUTPUT_PORT;
	encodingFormat.eCompressionFormat = OMX_VIDEO_CodingAVC;
	error = OMX_SetParameter(encoder, OMX_IndexParamVideoPortFormat, &encodingFormat);
    
    
    
    OMX_VIDEO_PARAM_AVCTYPE avcConfig;
    OMX_INIT_STRUCTURE(avcConfig);
    avcConfig.nPortIndex = ENCODER_OUTPUT_PORT;
    error = OMX_SetParameter(encoder, OMX_IndexParamVideoAvc, &avcConfig);
    OMX_TRACE(error);

#if 0
    stringstream avcConfigInfo;
    avcConfigInfo << "AVC INFO:\n";
    avcConfigInfo << "nBufferCountMin: "       << encoderOutputPortDefinition.nBufferCountMin      << "\n";
    avcConfigInfo << "nBufferCountActual: "    << encoderOutputPortDefinition.nBufferCountActual   << "\n";
    avcConfigInfo << "nBufferSize: "           << encoderOutputPortDefinition.nBufferSize          << "\n";
    avcConfigInfo << "nBufferAlignment: "      << encoderOutputPortDefinition.nBufferAlignment     << "\n";
    avcConfigInfo << "Compression Format: "    << OMX_Maps::getInstance().getVideoCoding(encoderOutputPortDefinition.format.video.eCompressionFormat) << "\n";
    
    encoderPortInfo << "Color Format: " << OMX_Maps::getInstance().getColorFormat(encoderOutputPortDefinition.format.video.eColorFormat) << "\n";
    
    ofLogVerbose(__func__) << encoderPortInfo.str();
 

    typedef struct OMX_VIDEO_PARAM_AVCTYPE {
        OMX_U32 nSize;                 
        OMX_VERSIONTYPE nVersion;      
        OMX_U32 nPortIndex;            
        OMX_U32 nSliceHeaderSpacing;  
        OMX_U32 nPFrames;     
        OMX_U32 nBFrames;     
        OMX_BOOL bUseHadamard;
        OMX_U32 nRefFrames;  
        OMX_U32 nRefIdx10ActiveMinus1;
        OMX_U32 nRefIdx11ActiveMinus1;
        OMX_BOOL bEnableUEP;  
        OMX_BOOL bEnableFMO;  
        OMX_BOOL bEnableASO;  
        OMX_BOOL bEnableRS;   
        OMX_VIDEO_AVCPROFILETYPE eProfile;
        OMX_VIDEO_AVCLEVELTYPE eLevel; 
        OMX_U32 nAllowedPictureTypes;  
        OMX_BOOL bFrameMBsOnly;        									
        OMX_BOOL bMBAFF;               
        OMX_BOOL bEntropyCodingCABAC;  
        OMX_BOOL bWeightedPPrediction; 
        OMX_U32 nWeightedBipredicitonMode; 
        OMX_BOOL bconstIpred ;
        OMX_BOOL bDirect8x8Inference;  
        OMX_BOOL bDirectSpatialTemporal;
        OMX_U32 nCabacInitIdc;
        OMX_VIDEO_AVCLOOPFILTERTYPE eLoopFilterMode;
    } OMX_VIDEO_PARAM_AVCTYPE;
#endif
    
#if 0   
    for(size_t i=0; i<OMX_Maps::getInstance().getColorFormatNames().size(); ++i)
    {
        string currentName = OMX_Maps::getInstance().getColorFormatNames()[i];
        
        encodingFormat.eColorFormat = OMX_Maps::getInstance().getColorFormat(currentName);
        error = OMX_SetParameter(encoder, OMX_IndexParamVideoPortFormat, &encodingFormat);
        OMX_TRACE(error);
        if(error == OMX_ErrorNone)
        {
            ofLogVerbose() << currentName << " SUPPORTED!";
        }
        
    }
#endif    
    
    return error;

}

inline
void CameraEngine::threadedFunction()
{
	while (isThreadRunning()) 
	{
		if(bufferAvailable) 
		{
			// The user wants to quit, but don't exit
			// the loop until we are certain that we have processed
			// a full frame till end of the frame, i.e. we're at the end
			// of the current key frame if processing one or until
			// the next key frame is detected. This way we should always
			// avoid corruption of the last encoded at the expense of
			// small delay in exiting.
			if(stopRequested && !isStopping) 
			{
				//ofLogVerbose(__func__) << "Stop Requested, waiting for next key frame boundry...";
				isStopping = true;
				isKeyframeValid = encoderOutputBuffer->nFlags & OMX_BUFFERFLAG_SYNCFRAME;
			}
			if(isStopping &&
               (isKeyframeValid ^ (encoderOutputBuffer->nFlags & OMX_BUFFERFLAG_SYNCFRAME))
               ) 
			{
				//ofLogVerbose(__func__) << "Key frame boundry reached, exiting loop...";
                doFillBuffer = false;
				writeFile();
				
				
			}else 
			{
				recordingFileBuffer.append((const char*) encoderOutputBuffer->pBuffer + encoderOutputBuffer->nOffset, 
                                           encoderOutputBuffer->nFilledLen);
                //ofLogVerbose() << "recordingFileBuffer: " << recordingFileBuffer.size();
                doFillBuffer = true;
                
			}
		}
		// Buffer flushed, request a new buffer to be filled by the encoder component
		if(doFillBuffer) 
		{
			doFillBuffer	= false;
			bufferAvailable = false;
			OMX_ERRORTYPE error = OMX_FillThisBuffer(encoder, encoderOutputBuffer);
            OMX_TRACE(error);
			if(error != OMX_ErrorNone) 
			{
				//ofLog(OF_LOG_ERROR, "encoder OMX_FillThisBuffer FAIL error: ",  omxErrorToCString(error) );
				closeEngine();
				
			}
		}
	}
}

inline
OMX_ERRORTYPE CameraEngine::onCameraEventParamOrConfigChanged()
{
    
    OMX_ERRORTYPE error = OMX_SendCommand(camera, OMX_CommandStateSet, OMX_StateIdle, NULL);
    OMX_TRACE(error, "camera->OMX_StateIdle");


    OMX_CONFIG_CAMERASENSORMODETYPE sensorConfig;
    OMX_INIT_STRUCTURE(sensorConfig);
    sensorConfig.nPortIndex = OMX_ALL;
    //sensorConfig.nModeIndex = 0;
    error =  OMX_GetParameter(camera, OMX_IndexConfigCameraSensorModes, &sensorConfig);
    OMX_TRACE(error);
    if(error == OMX_ErrorNone)
    {
        stringstream sensorInfo;
        sensorInfo << "nModeIndex: "  << sensorConfig.nModeIndex << endl;
        sensorInfo << "nNumModes: "  << sensorConfig.nNumModes << endl;
        sensorInfo << "nWidth: "  << sensorConfig.nWidth << endl;
        sensorInfo << "nHeight: "  << sensorConfig.nHeight << endl;
        sensorInfo << "nPaddingRight: "  << sensorConfig.nPaddingRight << endl;
        sensorInfo << "eColorFormat: "  << OMX_Maps::getInstance().getColorFormat(sensorConfig.eColorFormat) << endl;
        sensorInfo << "nFrameRateMax: "  << sensorConfig.nFrameRateMax << endl;
        sensorInfo << "nFrameRateMin: "  << sensorConfig.nFrameRateMin << endl;
        
        ofLogVerbose(__func__) << "sensorInfo: \n" << sensorInfo.str();
    }
    
    
    
    int numModes = sensorConfig.nNumModes;
    for (int i = 0; i < numModes; i++) 
    {
        sensorConfig.nModeIndex = i;
        error = OMX_GetParameter(camera, OMX_IndexConfigCameraSensorModes, &sensorConfig);
        OMX_TRACE(error);
        stringstream sensorInfo;
        sensorInfo << "nModeIndex: "  << sensorConfig.nModeIndex << endl;
        sensorInfo << "nWidth: "  << sensorConfig.nWidth << endl;
        sensorInfo << "nHeight: "  << sensorConfig.nHeight << endl;
        sensorInfo << "nPaddingRight: "  << sensorConfig.nPaddingRight << endl;
        sensorInfo << "eColorFormat: "  << OMX_Maps::getInstance().getColorFormat(sensorConfig.eColorFormat) << endl;
        sensorInfo << "nFrameRateMax: "  << sensorConfig.nFrameRateMax << endl;
        sensorInfo << "nFrameRateMin: "  << sensorConfig.nFrameRateMin << endl;
        sensorInfo << "FrameRateMax: " << (sensorConfig.nFrameRateMax / 256.0f) << endl;
        sensorInfo << "FrameRateMin: " << (sensorConfig.nFrameRateMin / 256.0f) << endl;
        ofLogVerbose(__func__) << "sensorInfo "<< i << " : \n" << sensorInfo.str();
    }
    
    //Enable Camera Output Port
    OMX_CONFIG_PORTBOOLEANTYPE cameraport;
    OMX_INIT_STRUCTURE(cameraport);
    cameraport.nPortIndex = cameraOutputPort;
    cameraport.bEnabled = OMX_TRUE;
    
    error =OMX_SetParameter(camera, OMX_IndexConfigPortCapturing, &cameraport);	
    OMX_TRACE(error);
    
    if(sessionConfig->doRecording)
    {
        OMX_CALLBACKTYPE splitterCallbacks;
        splitterCallbacks.EventHandler      = &CameraEngine::nullEventHandlerCallback;
        splitterCallbacks.EmptyBufferDone	= &CameraEngine::nullEmptyBufferDone;
        splitterCallbacks.FillBufferDone	= &CameraEngine::nullFillBufferDone;
        //Set up video splitter
        error = OMX_GetHandle(&splitter, OMX_VIDEO_SPLITTER, this, &splitterCallbacks);
        OMX_TRACE(error);
        error = DisableAllPortsForComponent(&splitter);
        OMX_TRACE(error);
        //Set splitter to Idle
        error = OMX_SendCommand(splitter, OMX_CommandStateSet, OMX_StateIdle, NULL);
        OMX_TRACE(error);
    }
    
    
    
    //Set up texture renderer
    OMX_CALLBACKTYPE renderCallbacks;
    renderCallbacks.EventHandler	= &CameraEngine::nullEventHandlerCallback;
    renderCallbacks.EmptyBufferDone	= &CameraEngine::nullEmptyBufferDone;
    if(engineType == TEXTURE_ENGINE)
    {
        renderCallbacks.FillBufferDone	= &CameraEngine::egl_renderFillBufferDone;
    }else
    {
        renderCallbacks.FillBufferDone	= &CameraEngine::nullFillBufferDone;

    }
    

    OMX_GetHandle(&render, engineTypeString, this , &renderCallbacks);
    DisableAllPortsForComponent(&render);
    
    
    
    //Set renderer to Idle
    error = OMX_SendCommand(render, OMX_CommandStateSet, OMX_StateIdle, NULL);
    OMX_TRACE(error);
    
    
    if(sessionConfig->doRecording)
    {
        //Create encoder
        
        OMX_CALLBACKTYPE encoderCallbacks;
        encoderCallbacks.EventHandler		= &CameraEngine::nullEventHandlerCallback;
        encoderCallbacks.EmptyBufferDone	= &CameraEngine::nullEmptyBufferDone;
        encoderCallbacks.FillBufferDone		= &CameraEngine::encoderFillBufferDone;
        
        error =OMX_GetHandle(&encoder, OMX_VIDEO_ENCODER, this , &encoderCallbacks);
        OMX_TRACE(error);
        
        
        configureEncoder();
        
    }
    
    
    
    if(sessionConfig->doRecording)
    {
        //Create camera->splitter Tunnel
        error = OMX_SetupTunnel(camera, cameraOutputPort, splitter, VIDEO_SPLITTER_INPUT_PORT);
        OMX_TRACE(error);
        
        
        // Tunnel splitter2 output port and encoder input port
        error = OMX_SetupTunnel(splitter, VIDEO_SPLITTER_OUTPUT_PORT2, encoder, ENCODER_INPUT_PORT);
        OMX_TRACE(error);
        
        
        //Create splitter->render Tunnel
        error = OMX_SetupTunnel(splitter, VIDEO_SPLITTER_OUTPUT_PORT1, render, renderInputPort);
        OMX_TRACE(error);
        
    }else 
    {
        //Create camera->render Tunnel
        error = OMX_SetupTunnel(camera, cameraOutputPort, render, renderInputPort);
        OMX_TRACE(error);
        
    }
    
    
    
    //Enable camera output port
    error = OMX_SendCommand(camera, OMX_CommandPortEnable, cameraOutputPort, NULL);
    OMX_TRACE(error);
    
    if(sessionConfig->doRecording)
    {
        //Enable splitter input port
        error = OMX_SendCommand(splitter, OMX_CommandPortEnable, VIDEO_SPLITTER_INPUT_PORT, NULL);
        OMX_TRACE(error);
        
        
        //Enable splitter output port
        error = OMX_SendCommand(splitter, OMX_CommandPortEnable, VIDEO_SPLITTER_OUTPUT_PORT1, NULL);
        OMX_TRACE(error);
        
        
        //Enable splitter output2 port
        error = OMX_SendCommand(splitter, OMX_CommandPortEnable, VIDEO_SPLITTER_OUTPUT_PORT2, NULL);
        OMX_TRACE(error);
        
    }
    
    
    if (engineType == TEXTURE_ENGINE)
    {
        //Enable render output port
        error = OMX_SendCommand(render, OMX_CommandPortEnable, EGL_RENDER_OUTPUT_PORT, NULL);
        OMX_TRACE(error);
        

    }
 
    
    
    //Enable render input port
    error = OMX_SendCommand(render, OMX_CommandPortEnable, renderInputPort, NULL);
    OMX_TRACE(error);
    
    
    /*
     Boolean parameter to enable/disable EGL discard mode. With discard mode enabled (default), EGL render will only buffer up to one image. If a new image is received while an image is waiting to be processed, the old image will be dropped. With discard mode disabled, 32 VC images (used in tunnelled mode) can be buffered. Once the buffer is full, the upstream component is notified and should attempt to send the image again later. Non-discard mode only applies to the tunnelled case (it does not apply when called internally).
     */
    if (engineType == TEXTURE_ENGINE)
    {  
         bool disableDiscardMode = false;
         if(disableDiscardMode)
         {
             OMX_CONFIG_PORTBOOLEANTYPE discardMode;
             OMX_INIT_STRUCTURE(discardMode);
             discardMode.nPortIndex = EGL_RENDER_INPUT_PORT;
             discardMode.bEnabled = OMX_FALSE; //default true
             error = OMX_SetParameter(render, OMX_IndexParamBrcmVideoEGLRenderDiscardMode, &discardMode);
             OMX_TRACE(error);
         
         }
    }

    
    if(sessionConfig->doRecording)
    {
        //Enable encoder input port
        error = OMX_SendCommand(encoder, OMX_CommandPortEnable, ENCODER_INPUT_PORT, NULL);
        OMX_TRACE(error);
        
        
        //Enable encoder output port
        error = OMX_SendCommand(encoder, OMX_CommandPortEnable, ENCODER_OUTPUT_PORT, NULL);
        OMX_TRACE(error);
        
        
        //Set encoder to Idle
        error = OMX_SendCommand(encoder, OMX_CommandStateSet, OMX_StateIdle, NULL);
        OMX_TRACE(error);
        
        // Configure encoder output buffer
        OMX_PARAM_PORTDEFINITIONTYPE encoderOutputPortDefinition;
        OMX_INIT_STRUCTURE(encoderOutputPortDefinition);
        encoderOutputPortDefinition.nPortIndex = ENCODER_OUTPUT_PORT;
        error =OMX_GetParameter(encoder, OMX_IndexParamPortDefinition, &encoderOutputPortDefinition);
        if (error != OMX_ErrorNone) 
        {
            OMX_TRACE(error, "encoder OMX_GetParameter OMX_IndexParamPortDefinition FAIL");
        }else 
        {
            ofLogVerbose(__func__) << "encoderOutputPortDefinition buffer info";
            ofLog(OF_LOG_VERBOSE, 
                  "nBufferCountMin(%u)					\n \
                  nBufferCountActual(%u)				\n \
                  nBufferSize(%u)						\n \
                  nBufferAlignmen(%u) \n", 
                  encoderOutputPortDefinition.nBufferCountMin, 
                  encoderOutputPortDefinition.nBufferCountActual, 
                  encoderOutputPortDefinition.nBufferSize, 
                  encoderOutputPortDefinition.nBufferAlignment);
            
        }
        
        error =  OMX_AllocateBuffer(encoder, &encoderOutputBuffer, ENCODER_OUTPUT_PORT, NULL, encoderOutputPortDefinition.nBufferSize);
        OMX_TRACE(error);
    }
    
    if (engineType == TEXTURE_ENGINE)
    {
        
        //Set renderer to use texture
        error = OMX_UseEGLImage(render, &eglBuffer, EGL_RENDER_OUTPUT_PORT, this, eglImage);
        OMX_TRACE(error);
    }
    
    
    //Start renderer
    error = OMX_SendCommand(render, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    OMX_TRACE(error);
    
    
    if(sessionConfig->doRecording)
    {
        //Start encoder
        error = OMX_SendCommand(encoder, OMX_CommandStateSet, OMX_StateExecuting, NULL);
        OMX_TRACE(error);
        
        
        //Start splitter
        error = OMX_SendCommand(splitter, OMX_CommandStateSet, OMX_StateExecuting, NULL);
        OMX_TRACE(error);
        
    }
    
    
    
    //Start camera
    error = OMX_SendCommand(camera, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    OMX_TRACE(error);
    
    
    if (engineType == TEXTURE_ENGINE)
    {
        //start the buffer filling loop
        //once completed the callback will trigger and refill
        error = OMX_FillThisBuffer(render, eglBuffer);
        OMX_TRACE(error);
    }
    
    
    if(sessionConfig->doRecording)
    {
        error = OMX_FillThisBuffer(encoder, encoderOutputBuffer);
        OMX_TRACE(error);
        
        bool doThreadBlocking	= true;
        startThread(doThreadBlocking);
        isCurrentlyRecording = true;
    }
    if (engineType == NON_TEXTURE_ENGINE)
    {
        setupDisplay();
    }

    didOpen = true;
    return error;
}

inline
OMX_ERRORTYPE CameraEngine::setupDisplay()
{
    
    OMX_CONFIG_DISPLAYREGIONTYPE region;
    
    OMX_INIT_STRUCTURE(region);
    region.nPortIndex = VIDEO_RENDER_INPUT_PORT; /* Video render input port */
    
    region.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_DEST_RECT | OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_NOASPECT);
    
    region.fullscreen = OMX_FALSE;
    region.noaspect = OMX_TRUE;
    
    region.dest_rect.x_offset = 0;
    region.dest_rect.y_offset = 0;
    region.dest_rect.width	= sessionConfig->width;
    region.dest_rect.height = sessionConfig->height;
    
    OMX_ERRORTYPE error  = OMX_SetParameter(render, OMX_IndexConfigDisplayRegion, &region);
    
    OMX_TRACE(error, "render OMX_IndexConfigDisplayRegion");
    
    
    return error;
    
}

void CameraEngine::stopRecording()
{
	
	if(isCurrentlyRecording)
	{
        encoderOutputBuffer->nFlags = OMX_BUFFERFLAG_EOS;
        OMX_ERRORTYPE error = OMX_FillThisBuffer(encoder, encoderOutputBuffer);
        OMX_TRACE(error);
		stopRequested = true;
	}	
}

inline
bool CameraEngine::writeFile()
{

	stopThread();
    isCurrentlyRecording = false;
	stringstream fileName;
	fileName << ofGetTimestampString() << "_";
	
	fileName << sessionConfig->width << "x";
	fileName << sessionConfig->height << "_";
	fileName << sessionConfig->framerate << "fps_";
	
	fileName << numMBps << "MBps_";
	
	fileName << recordedFrameCounter << "numFrames_";
	
    switch(engineType)
    {
        case TEXTURE_ENGINE:
        {
            fileName << "TEXTURE_ENGINE";
            break;
        }
        case NON_TEXTURE_ENGINE:
        {
            fileName << "NON_TEXTURE_ENGINE";
            break;
        }
    }
	
	fileName << ".h264";
	
	string filePath;
	
	if (sessionConfig->recordingFilePath == "") 
	{
		filePath = ofToDataPath(fileName.str(), true);
	}else
	{
		filePath = sessionConfig->recordingFilePath;
	}
	
	didWriteFile = ofBufferToFile(filePath, recordingFileBuffer, true);
    ofLogVerbose(__func__) << "didWriteFile: " << didWriteFile << " filePath: " << filePath;
    
    recordingFileBuffer.clear();
    return didWriteFile;
}

CameraEngine::~CameraEngine()
{
    if(didOpen)
    {
        closeEngine();
    }
}

inline
void CameraEngine::closeEngine()
{
    if(isCurrentlyRecording && !didWriteFile)
    {
        writeFile();
        
    }
    
    OMX_ERRORTYPE error;
    error =  OMX_SendCommand(camera, OMX_CommandFlush, cameraOutputPort, NULL);
    OMX_TRACE(error, "camera: OMX_CommandFlush");
    
    if(encoder)
    {
        error =  OMX_SendCommand(encoder, OMX_CommandFlush, ENCODER_INPUT_PORT, NULL);
        OMX_TRACE(error, "encoder: OMX_CommandFlush ENCODER_INPUT_PORT");
        error =  OMX_SendCommand(encoder, OMX_CommandFlush, ENCODER_OUTPUT_PORT, NULL);
        OMX_TRACE(error, "encoder: OMX_CommandFlush ENCODER_OUTPUT_PORT");
    }
    
    //DisableAllPortsForComponent
    error = DisableAllPortsForComponent(&camera, "camera");
    OMX_TRACE(error, "DisableAllPortsForComponent: camera");
    
    if(splitter)
    {
        error = DisableAllPortsForComponent(&splitter, "splitter");
        OMX_TRACE(error, "DisableAllPortsForComponent splitter");
    }
    
    if(encoder)
    {
        error = DisableAllPortsForComponent(&encoder, "encoder");
        OMX_TRACE(error, "DisableAllPortsForComponent encoder");
    }
    
    switch(engineType)
    {
        case TEXTURE_ENGINE:
        {
            error = OMX_FreeBuffer(render, EGL_RENDER_OUTPUT_PORT, eglBuffer);
            OMX_TRACE(error, "OMX_FreeBuffer(render, EGL_RENDER_OUTPUT_PORT");
            error = DisableAllPortsForComponent(&render, "egl_render");
            OMX_TRACE(error, "DisableAllPortsForComponent: render");
            break;
        }
        case NON_TEXTURE_ENGINE:
        {
            error = DisableAllPortsForComponent(&render, "video_render");
            OMX_TRACE(error, "DisableAllPortsForComponent: render");
            break;
        }
    }
    
    

    
    //OMX_FreeBuffer
    if(encoder)
    {
        error = OMX_FreeBuffer(encoder, ENCODER_OUTPUT_PORT, encoderOutputBuffer);
        OMX_TRACE(error, "OMX_FreeBuffer(encoder, ENCODER_OUTPUT_PORT");
    }
    
    
    //OMX_StateIdle
    error = OMX_SendCommand(camera, OMX_CommandStateSet, OMX_StateIdle, NULL);
    OMX_TRACE(error, "camera->OMX_StateIdle");
    
    if(splitter)
    {
        error = OMX_SendCommand(splitter, OMX_CommandStateSet, OMX_StateIdle, NULL);
        OMX_TRACE(error, "splitter->OMX_StateIdle");
    }
    
    
    if(encoder)
    {
        error = OMX_SendCommand(encoder, OMX_CommandStateSet, OMX_StateIdle, NULL);
        OMX_TRACE(error, "encoder->OMX_StateIdle");
        
        
        OMX_STATETYPE encoderState;
        error = OMX_GetState(encoder, &encoderState);
        OMX_TRACE(error, "encoderState: "+ getStateString(encoderState));
    }
    
    error = OMX_SendCommand(render, OMX_CommandStateSet, OMX_StateIdle, NULL);
    OMX_TRACE(error, "render->OMX_StateIdle");
    
   
    
    //OMX_StateLoaded
    error = OMX_SendCommand(camera, OMX_CommandStateSet, OMX_StateLoaded, NULL);
    OMX_TRACE(error, "camera->OMX_StateLoaded");
    
    if(splitter)
    {
        error = OMX_SendCommand(splitter, OMX_CommandStateSet, OMX_StateLoaded, NULL);
        OMX_TRACE(error, "splitter->OMX_StateLoaded");
    }
    
    if(encoder)
    {
        error = OMX_SendCommand(encoder, OMX_CommandStateSet, OMX_StateLoaded, NULL);
        OMX_TRACE(error, "encoder->OMX_StateLoaded");
    }
    
    error = OMX_SendCommand(render, OMX_CommandStateSet, OMX_StateLoaded, NULL);
    OMX_TRACE(error, "render->OMX_StateLoaded");
    
 
    //OMX_FreeHandle
    error = OMX_FreeHandle(camera);
    OMX_TRACE(error, "OMX_FreeHandle(camera)");
    
    if(splitter)
    {
        error = OMX_FreeHandle(splitter);
        OMX_TRACE(error, "OMX_FreeHandle(splitter)");
    }
    
    if(encoder)
    {
        error = OMX_FreeHandle(encoder);
        OMX_TRACE(error, "OMX_FreeHandle(encoder)"); 
    }  
    
    error =  OMX_FreeHandle(render);
    OMX_TRACE(error, "OMX_FreeHandle(render)");
    
    sessionConfig = NULL;
    didOpen = false;

}

