/* JNI bridge between com.pixovr.apex.ApexNativeBridge and the core's C
 * interface (apex-sdk-core/include/apex/Apex.h). The core is linked in
 * statically, with its java.net HTTP backend, so this library is the only
 * native code the SDK loads, on desktops and on Android alike.
 *
 * Strings cross as UTF-8 byte arrays in both directions: JNI's own string
 * functions use modified UTF-8, which differs for characters outside the BMP.
 * Every native method runs on the client's owner thread (see ApexClient),
 * which is a Java thread, so callbacks reach Java through GetEnv. */

#include "apex/Apex.h"

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BRIDGE_CLASS "com/pixovr/apex/ApexNativeBridge"
#define LISTENER_CLASS BRIDGE_CLASS "$Listener"

/* A client and the Java listener its callbacks go to. Owned by the Java
 * ApexClient through its handle, freed by NativeDestroy. */
typedef struct BridgeClient
{
	ApexClient* Client;
	jobject Listener; /* global reference */
} BridgeClient;

static JavaVM* bridgeJavaVm = NULL;
static jmethodID listenerOnResult = NULL;
static jmethodID listenerOnLog = NULL;
static jmethodID listenerOnTelemetryPacket = NULL;

/* ------------------------------------------------------------------------ */
/* Conversions                                                               */
/* ------------------------------------------------------------------------ */

/* Copies a Java byte[] into a NUL-terminated buffer the caller frees. NULL for
 * a null array (or when out of memory). */
static char* CopyJavaBytes(JNIEnv* jniEnvironment, jbyteArray javaBytes, size_t* byteCount)
{
	jsize javaByteCount;
	char* nativeBytes;
	if (byteCount != NULL)
	{
		*byteCount = 0;
	}

	if (javaBytes == NULL)
	{
		return NULL;
	}

	javaByteCount = (*jniEnvironment)->GetArrayLength(jniEnvironment, javaBytes);
	nativeBytes = (char*)malloc((size_t)javaByteCount + 1);
	if (nativeBytes == NULL)
	{
		return NULL;
	}

	(*jniEnvironment)->GetByteArrayRegion(jniEnvironment, javaBytes, 0, javaByteCount, (jbyte*)nativeBytes);
	nativeBytes[javaByteCount] = '\0';
	if (byteCount != NULL)
	{
		*byteCount = (size_t)javaByteCount;
	}

	return nativeBytes;
}

static jbyteArray MakeJavaBytes(JNIEnv* jniEnvironment, const char* nativeBytes, size_t byteCount)
{
	jbyteArray javaBytes = (*jniEnvironment)->NewByteArray(jniEnvironment, (jsize)byteCount);
	if (javaBytes != NULL && byteCount > 0)
	{
		(*jniEnvironment)
			->SetByteArrayRegion(jniEnvironment, javaBytes, 0, (jsize)byteCount, (const jbyte*)nativeBytes);
	}

	return javaBytes;
}

/* NULL text gives a null array. */
static jbyteArray MakeJavaText(JNIEnv* jniEnvironment, const char* text)
{
	return text != NULL ? MakeJavaBytes(jniEnvironment, text, strlen(text)) : NULL;
}

static ApexClient* GetClient(jlong clientHandle)
{
	BridgeClient* bridgeClient = (BridgeClient*)(intptr_t)clientHandle;
	return bridgeClient != NULL ? bridgeClient->Client : NULL;
}

static ApexPose ConvertPose(JNIEnv* jniEnvironment, jfloatArray javaPose)
{
	/* Position x, y, z, then rotation x, y, z. */
	jfloat poseValues[6] = {0};
	ApexPose pose;
	if (javaPose != NULL && (*jniEnvironment)->GetArrayLength(jniEnvironment, javaPose) >= 6)
	{
		(*jniEnvironment)->GetFloatArrayRegion(jniEnvironment, javaPose, 0, 6, poseValues);
	}

	memcpy(pose.Position, poseValues, sizeof(pose.Position));
	memcpy(pose.Rotation, poseValues + 3, sizeof(pose.Rotation));
	return pose;
}

/* ------------------------------------------------------------------------ */
/* Callbacks from the core                                                   */
/* ------------------------------------------------------------------------ */

static JNIEnv* GetCallbackEnvironment(void)
{
	JNIEnv* jniEnvironment = NULL;
	if (bridgeJavaVm == NULL ||
		(*bridgeJavaVm)->GetEnv(bridgeJavaVm, (void**)&jniEnvironment, JNI_VERSION_1_6) != JNI_OK)
	{
		return NULL;
	}

	return jniEnvironment;
}

/* A listener that throws must not leave the exception pending for the core's
 * next JNI call; the Java side catches its own, so this is a last resort. */
static void ClearListenerException(JNIEnv* jniEnvironment)
{
	if ((*jniEnvironment)->ExceptionCheck(jniEnvironment))
	{
		(*jniEnvironment)->ExceptionDescribe(jniEnvironment);
		(*jniEnvironment)->ExceptionClear(jniEnvironment);
	}
}

static void APEX_CALL OnResult(void* userData, const ApexResult* result)
{
	BridgeClient* bridgeClient = (BridgeClient*)userData;
	JNIEnv* jniEnvironment = GetCallbackEnvironment();
	jbyteArray javaMessage;
	jbyteArray javaJson;
	if (jniEnvironment == NULL)
	{
		return;
	}

	javaMessage = MakeJavaText(jniEnvironment, result->Message);
	javaJson = MakeJavaText(jniEnvironment, result->Json);
	(*jniEnvironment)
		->CallVoidMethod(jniEnvironment, bridgeClient->Listener, listenerOnResult, (jlong)result->CallId,
						 (jint)result->Error, (jint)result->HttpStatus, javaMessage, javaJson);
	ClearListenerException(jniEnvironment);
	(*jniEnvironment)->DeleteLocalRef(jniEnvironment, javaMessage);
	(*jniEnvironment)->DeleteLocalRef(jniEnvironment, javaJson);
}

static void APEX_CALL OnLog(void* userData, int32_t logLevel, const char* message)
{
	BridgeClient* bridgeClient = (BridgeClient*)userData;
	JNIEnv* jniEnvironment = GetCallbackEnvironment();
	jbyteArray javaMessage;
	if (jniEnvironment == NULL)
	{
		return;
	}

	javaMessage = MakeJavaText(jniEnvironment, message);
	(*jniEnvironment)
		->CallVoidMethod(jniEnvironment, bridgeClient->Listener, listenerOnLog, (jint)logLevel, javaMessage);
	ClearListenerException(jniEnvironment);
	(*jniEnvironment)->DeleteLocalRef(jniEnvironment, javaMessage);
}

static void APEX_CALL OnTelemetryPacket(void* userData, const char* packetJson)
{
	BridgeClient* bridgeClient = (BridgeClient*)userData;
	JNIEnv* jniEnvironment = GetCallbackEnvironment();
	jbyteArray javaPacket;
	if (jniEnvironment == NULL)
	{
		return;
	}

	javaPacket = MakeJavaText(jniEnvironment, packetJson);
	(*jniEnvironment)->CallVoidMethod(jniEnvironment, bridgeClient->Listener, listenerOnTelemetryPacket, javaPacket);
	ClearListenerException(jniEnvironment);
	(*jniEnvironment)->DeleteLocalRef(jniEnvironment, javaPacket);
}

/* ------------------------------------------------------------------------ */
/* Client                                                                    */
/* ------------------------------------------------------------------------ */

/* The text fields of ApexConfig, copied out of their Java byte arrays. */
typedef enum EConfigText
{
	CONFIG_TEXT_HEADSET_API_URL,
	CONFIG_TEXT_PLATFORM_API_URL,
	CONFIG_TEXT_EVENT_PATH,
	CONFIG_TEXT_MODULE_VERSION,
	CONFIG_TEXT_DEVICE_ID,
	CONFIG_TEXT_DEVICE_MODEL,
	CONFIG_TEXT_PLATFORM,
	CONFIG_TEXT_CLIENT_IP,
	CONFIG_TEXT_TELEMETRY_URL,
	CONFIG_TEXT_COUNT
} EConfigText;

static jlong JNICALL NativeCreate(JNIEnv* jniEnvironment, jclass bridgeClass, jint environment,
								  jbyteArray headsetApiUrl, jbyteArray platformApiUrl, jbyteArray eventPath,
								  jint moduleId, jbyteArray moduleVersion, jbyteArray deviceId, jbyteArray deviceModel,
								  jbyteArray platform, jbyteArray clientIp, jdouble heartbeatIntervalSeconds,
								  jint requestTimeoutMilliseconds, jint logLevel, jint telemetrySink,
								  jbyteArray telemetryUrl, jdouble telemetryFlushIntervalSeconds,
								  jdouble telemetryPoseSamplesPerSecond, jint telemetryMaximumRecordsPerPacket,
								  jint sessionFormat, jobject listener)
{
	const jbyteArray javaTexts[CONFIG_TEXT_COUNT] = {headsetApiUrl, platformApiUrl, eventPath, moduleVersion, deviceId,
													 deviceModel,   platform,       clientIp,  telemetryUrl};
	char* configTexts[CONFIG_TEXT_COUNT];
	BridgeClient* bridgeClient = (BridgeClient*)calloc(1, sizeof(BridgeClient));
	ApexConfig config;
	int textIndex;
	(void)bridgeClass;

	if (bridgeClient == NULL || listener == NULL)
	{
		free(bridgeClient);
		return 0;
	}

	for (textIndex = 0; textIndex < CONFIG_TEXT_COUNT; textIndex++)
	{
		configTexts[textIndex] = CopyJavaBytes(jniEnvironment, javaTexts[textIndex], NULL);
	}

	ApexConfigInit(&config);
	config.Environment = environment;
	config.HeadsetApiUrl = configTexts[CONFIG_TEXT_HEADSET_API_URL];
	config.PlatformApiUrl = configTexts[CONFIG_TEXT_PLATFORM_API_URL];
	config.EventPath = configTexts[CONFIG_TEXT_EVENT_PATH];
	config.ModuleId = moduleId;
	config.ModuleVersion = configTexts[CONFIG_TEXT_MODULE_VERSION];
	config.DeviceId = configTexts[CONFIG_TEXT_DEVICE_ID];
	config.DeviceModel = configTexts[CONFIG_TEXT_DEVICE_MODEL];
	config.Platform = configTexts[CONFIG_TEXT_PLATFORM];
	config.ClientIp = configTexts[CONFIG_TEXT_CLIENT_IP];
	if (heartbeatIntervalSeconds > 0.0)
	{
		config.HeartbeatIntervalSeconds = heartbeatIntervalSeconds;
	}

	if (requestTimeoutMilliseconds > 0)
	{
		config.RequestTimeoutMilliseconds = (uint32_t)requestTimeoutMilliseconds;
	}

	config.LogCallback = OnLog;
	config.LogUserData = bridgeClient;
	config.LogLevel = logLevel;
	config.TelemetrySink = telemetrySink;
	config.TelemetryUrl = configTexts[CONFIG_TEXT_TELEMETRY_URL];
	config.TelemetryCallback = OnTelemetryPacket;
	config.TelemetryUserData = bridgeClient;
	config.TelemetryFlushIntervalSeconds = telemetryFlushIntervalSeconds;
	config.TelemetryPoseSamplesPerSecond = telemetryPoseSamplesPerSecond;
	config.TelemetryMaximumRecordsPerPacket = telemetryMaximumRecordsPerPacket;
	config.SessionFormat = sessionFormat;

	bridgeClient->Listener = (*jniEnvironment)->NewGlobalRef(jniEnvironment, listener);
	bridgeClient->Client = ApexClientCreate(&config);
	for (textIndex = 0; textIndex < CONFIG_TEXT_COUNT; textIndex++)
	{
		free(configTexts[textIndex]); /* the core copied them */
	}

	if (bridgeClient->Client == NULL)
	{
		(*jniEnvironment)->DeleteGlobalRef(jniEnvironment, bridgeClient->Listener);
		free(bridgeClient);
		return 0;
	}

	return (jlong)(intptr_t)bridgeClient;
}

static void JNICALL NativeDestroy(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle)
{
	BridgeClient* bridgeClient = (BridgeClient*)(intptr_t)clientHandle;
	(void)bridgeClass;
	if (bridgeClient == NULL)
	{
		return;
	}

	/* ApexClient never destroys from inside a callback, so the core is gone
	 * when this returns and no callback can reach the listener any more. */
	ApexClientDestroy(bridgeClient->Client);
	(*jniEnvironment)->DeleteGlobalRef(jniEnvironment, bridgeClient->Listener);
	free(bridgeClient);
}

static void JNICALL NativePoll(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle, jdouble nowSeconds)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	ApexClientPoll(GetClient(clientHandle), nowSeconds);
}

static void JNICALL NativeCancel(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle, jlong callId)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	ApexCancel(GetClient(clientHandle), (ApexCallId)callId);
}

/* ------------------------------------------------------------------------ */
/* Platform calls                                                            */
/* ------------------------------------------------------------------------ */

static jlong JNICALL NativePing(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	return (jlong)ApexPing(GetClient(clientHandle), OnResult, (void*)(intptr_t)clientHandle);
}

static jlong JNICALL NativeLogin(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle, jbyteArray username,
								 jbyteArray password)
{
	char* nativeUsername = CopyJavaBytes(jniEnvironment, username, NULL);
	char* nativePassword = CopyJavaBytes(jniEnvironment, password, NULL);
	const ApexCallId callId =
		ApexLogin(GetClient(clientHandle), nativeUsername, nativePassword, OnResult, (void*)(intptr_t)clientHandle);
	(void)bridgeClass;
	free(nativeUsername);
	if (nativePassword != NULL)
	{
		memset(nativePassword, 0, strlen(nativePassword));
	}

	free(nativePassword);
	return (jlong)callId;
}

static jlong JNICALL NativeLoginWithToken(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										  jbyteArray userToken)
{
	char* nativeToken = CopyJavaBytes(jniEnvironment, userToken, NULL);
	const ApexCallId callId =
		ApexLoginWithToken(GetClient(clientHandle), nativeToken, OnResult, (void*)(intptr_t)clientHandle);
	(void)bridgeClass;
	free(nativeToken);
	return (jlong)callId;
}

static jlong JNICALL NativeQuickIdGetUsers(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										   jbyteArray serialNumber)
{
	char* nativeSerialNumber = CopyJavaBytes(jniEnvironment, serialNumber, NULL);
	const ApexCallId callId =
		ApexQuickIdGetUsers(GetClient(clientHandle), nativeSerialNumber, OnResult, (void*)(intptr_t)clientHandle);
	(void)bridgeClass;
	free(nativeSerialNumber);
	return (jlong)callId;
}

static jlong JNICALL NativeQuickIdLogin(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										jbyteArray serialNumber, jbyteArray username)
{
	char* nativeSerialNumber = CopyJavaBytes(jniEnvironment, serialNumber, NULL);
	char* nativeUsername = CopyJavaBytes(jniEnvironment, username, NULL);
	const ApexCallId callId = ApexQuickIdLogin(GetClient(clientHandle), nativeSerialNumber, nativeUsername, OnResult,
											   (void*)(intptr_t)clientHandle);
	(void)bridgeClass;
	free(nativeSerialNumber);
	free(nativeUsername);
	return (jlong)callId;
}

static jlong JNICALL NativeGenerateLoginCode(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
											 jint userId)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	return (jlong)ApexGenerateLoginCode(GetClient(clientHandle), userId, OnResult, (void*)(intptr_t)clientHandle);
}

static void JNICALL NativeLogout(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	ApexLogout(GetClient(clientHandle));
}

static jlong JNICALL NativeGetUserModules(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle, jint userId)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	return (jlong)ApexGetUserModules(GetClient(clientHandle), userId, OnResult, (void*)(intptr_t)clientHandle);
}

static jlong JNICALL NativeCheckModuleAccess(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
											 jint moduleId)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	return (jlong)ApexCheckModuleAccess(GetClient(clientHandle), moduleId, OnResult, (void*)(intptr_t)clientHandle);
}

static jlong JNICALL NativeGetModuleList(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										 jbyteArray platformName)
{
	char* nativePlatformName = CopyJavaBytes(jniEnvironment, platformName, NULL);
	const ApexCallId callId =
		ApexGetModuleList(GetClient(clientHandle), nativePlatformName, OnResult, (void*)(intptr_t)clientHandle);
	(void)bridgeClass;
	free(nativePlatformName);
	return (jlong)callId;
}

static jlong JNICALL NativeGetModulePage(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										 jbyteArray platformName, jint lifecycleMask, jint sortOrder, jint pageIndex,
										 jint pageSize, jboolean refresh)
{
	char* nativePlatformName = CopyJavaBytes(jniEnvironment, platformName, NULL);
	const ApexCallId callId =
		ApexGetModulePage(GetClient(clientHandle), nativePlatformName, (uint32_t)lifecycleMask, sortOrder, pageIndex,
						  pageSize, refresh ? 1 : 0, OnResult, (void*)(intptr_t)clientHandle);
	(void)bridgeClass;
	free(nativePlatformName);
	return (jlong)callId;
}

/* ------------------------------------------------------------------------ */
/* Sessions                                                                  */
/* ------------------------------------------------------------------------ */

typedef ApexCallId(APEX_CALL* SessionCallFunction)(ApexClient* client, const void* moduleData, size_t moduleDataLength,
												   ApexResultCallback onResult, void* userData);

static jlong StartSessionCall(JNIEnv* jniEnvironment, jlong clientHandle, jbyteArray moduleData,
							  SessionCallFunction sessionCall)
{
	size_t moduleDataLength = 0;
	char* nativeModuleData = CopyJavaBytes(jniEnvironment, moduleData, &moduleDataLength);
	const ApexCallId callId = sessionCall(GetClient(clientHandle), nativeModuleData, moduleDataLength, OnResult,
										  (void*)(intptr_t)clientHandle);
	free(nativeModuleData);
	return (jlong)callId;
}

static jlong JNICALL NativeSessionJoin(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
									   jbyteArray moduleData)
{
	(void)bridgeClass;
	return StartSessionCall(jniEnvironment, clientHandle, moduleData, ApexSessionJoin);
}

static jlong JNICALL NativeSessionEvent(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										jbyteArray moduleData)
{
	(void)bridgeClass;
	return StartSessionCall(jniEnvironment, clientHandle, moduleData, ApexSessionEvent);
}

static jlong JNICALL NativeSessionComplete(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										   jbyteArray moduleData)
{
	(void)bridgeClass;
	return StartSessionCall(jniEnvironment, clientHandle, moduleData, ApexSessionComplete);
}

/* ------------------------------------------------------------------------ */
/* State and launch arguments                                                */
/* ------------------------------------------------------------------------ */

static jboolean JNICALL NativeIsLoggedIn(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	return ApexIsLoggedIn(GetClient(clientHandle)) ? JNI_TRUE : JNI_FALSE;
}

typedef size_t(APEX_CALL* TextReader)(const ApexClient* client, const char* key, char* buffer, size_t bufferSize);

/* Reads a text value with the query-then-fill convention of Apex.h. key is
 * passed on to readText; ApexGetCurrentUser, which takes none, goes through
 * ReadCurrentUser. */
static jbyteArray ReadText(JNIEnv* jniEnvironment, jlong clientHandle, const char* key, TextReader readText)
{
	const ApexClient* client = GetClient(clientHandle);
	const size_t textLength = readText(client, key, NULL, 0);
	char* textBuffer = (char*)malloc(textLength + 1);
	jbyteArray javaText;
	if (textBuffer == NULL)
	{
		return NULL;
	}

	readText(client, key, textBuffer, textLength + 1);
	javaText = MakeJavaBytes(jniEnvironment, textBuffer, textLength);
	free(textBuffer);
	return javaText;
}

static size_t APEX_CALL ReadCurrentUser(const ApexClient* client, const char* unusedKey, char* buffer,
										size_t bufferSize)
{
	(void)unusedKey;
	return ApexGetCurrentUser(client, buffer, bufferSize);
}

static jbyteArray JNICALL NativeGetCurrentUser(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle)
{
	(void)bridgeClass;
	return ReadText(jniEnvironment, clientHandle, NULL, ReadCurrentUser);
}

static jlong JNICALL NativeGetSessionId(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	return (jlong)ApexGetSessionId(GetClient(clientHandle));
}

static void JNICALL NativeSetDeviceId(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
									  jbyteArray deviceId)
{
	char* nativeDeviceId = CopyJavaBytes(jniEnvironment, deviceId, NULL);
	(void)bridgeClass;
	ApexSetDeviceId(GetClient(clientHandle), nativeDeviceId);
	free(nativeDeviceId);
}

static void JNICALL NativeSetSessionFormat(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										   jint sessionFormat)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	ApexSetSessionFormat(GetClient(clientHandle), sessionFormat);
}

static void JNICALL NativeLaunchParseArgs(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										  jobjectArray javaArguments)
{
	const jsize argumentCount =
		javaArguments != NULL ? (*jniEnvironment)->GetArrayLength(jniEnvironment, javaArguments) : 0;
	char** argumentValues = (char**)calloc((size_t)argumentCount + 1, sizeof(char*));
	jsize argumentIndex;
	(void)bridgeClass;
	if (argumentValues == NULL)
	{
		return;
	}

	for (argumentIndex = 0; argumentIndex < argumentCount; argumentIndex++)
	{
		jbyteArray javaArgument =
			(jbyteArray)(*jniEnvironment)->GetObjectArrayElement(jniEnvironment, javaArguments, argumentIndex);
		argumentValues[argumentIndex] = CopyJavaBytes(jniEnvironment, javaArgument, NULL);
		(*jniEnvironment)->DeleteLocalRef(jniEnvironment, javaArgument);
		if (argumentValues[argumentIndex] == NULL)
		{
			argumentValues[argumentIndex] = (char*)calloc(1, 1);
		}
	}

	ApexLaunchParseArgs(GetClient(clientHandle), (int32_t)argumentCount, (const char* const*)argumentValues);
	for (argumentIndex = 0; argumentIndex < argumentCount; argumentIndex++)
	{
		free(argumentValues[argumentIndex]);
	}

	free((void*)argumentValues);
}

static void JNICALL NativeLaunchParseUrl(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										 jbyteArray launchUrl)
{
	char* nativeLaunchUrl = CopyJavaBytes(jniEnvironment, launchUrl, NULL);
	(void)bridgeClass;
	ApexLaunchParseUrl(GetClient(clientHandle), nativeLaunchUrl);
	free(nativeLaunchUrl);
}

static void JNICALL NativeLaunchSet(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle, jbyteArray key,
									jbyteArray launchValue)
{
	char* nativeKey = CopyJavaBytes(jniEnvironment, key, NULL);
	char* nativeLaunchValue = CopyJavaBytes(jniEnvironment, launchValue, NULL);
	(void)bridgeClass;
	ApexLaunchSet(GetClient(clientHandle), nativeKey, nativeLaunchValue);
	free(nativeKey);
	free(nativeLaunchValue);
}

static jbyteArray JNICALL NativeLaunchGet(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										  jbyteArray key)
{
	char* nativeKey = CopyJavaBytes(jniEnvironment, key, NULL);
	jbyteArray launchValue = ReadText(jniEnvironment, clientHandle, nativeKey, ApexLaunchGet);
	(void)bridgeClass;
	free(nativeKey);
	return launchValue;
}

static jbyteArray JNICALL NativeLaunchExitTarget(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
												 jbyteArray nextTarget)
{
	char* nativeNextTarget = CopyJavaBytes(jniEnvironment, nextTarget, NULL);
	jbyteArray exitTarget = ReadText(jniEnvironment, clientHandle, nativeNextTarget, ApexLaunchExitTarget);
	(void)bridgeClass;
	free(nativeNextTarget);
	return exitTarget;
}

/* ------------------------------------------------------------------------ */
/* Telemetry                                                                 */
/* ------------------------------------------------------------------------ */

static void JNICALL NativeTelemetryObjectRegistered(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
													jbyteArray trackedId, jbyteArray objectName, jbyteArray meshName,
													jfloatArray javaPose)
{
	char* nativeTrackedId = CopyJavaBytes(jniEnvironment, trackedId, NULL);
	char* nativeObjectName = CopyJavaBytes(jniEnvironment, objectName, NULL);
	char* nativeMeshName = CopyJavaBytes(jniEnvironment, meshName, NULL);
	const ApexPose pose = ConvertPose(jniEnvironment, javaPose);
	(void)bridgeClass;
	ApexTelemetryObjectRegistered(GetClient(clientHandle), nativeTrackedId, nativeObjectName, nativeMeshName,
								  javaPose != NULL ? &pose : NULL);
	free(nativeTrackedId);
	free(nativeObjectName);
	free(nativeMeshName);
}

static void JNICALL NativeTelemetryObjectUnregistered(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
													  jbyteArray trackedId, jfloatArray javaPose)
{
	char* nativeTrackedId = CopyJavaBytes(jniEnvironment, trackedId, NULL);
	const ApexPose pose = ConvertPose(jniEnvironment, javaPose);
	(void)bridgeClass;
	ApexTelemetryObjectUnregistered(GetClient(clientHandle), nativeTrackedId, javaPose != NULL ? &pose : NULL);
	free(nativeTrackedId);
}

typedef void(APEX_CALL* TelemetryPairFunction)(ApexClient* client, const char* firstText, const char* secondText);

static void RecordTelemetryPair(JNIEnv* jniEnvironment, jlong clientHandle, jbyteArray firstText, jbyteArray secondText,
								TelemetryPairFunction recordPair)
{
	char* nativeFirstText = CopyJavaBytes(jniEnvironment, firstText, NULL);
	char* nativeSecondText = CopyJavaBytes(jniEnvironment, secondText, NULL);
	recordPair(GetClient(clientHandle), nativeFirstText, nativeSecondText);
	free(nativeFirstText);
	free(nativeSecondText);
}

static void JNICALL NativeTelemetryEngagementBegin(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
												   jbyteArray trackedId, jbyteArray engagementName)
{
	(void)bridgeClass;
	RecordTelemetryPair(jniEnvironment, clientHandle, trackedId, engagementName, ApexTelemetryEngagementBegin);
}

static void JNICALL NativeTelemetryEngagementEnd(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
												 jbyteArray trackedId, jbyteArray engagementName)
{
	(void)bridgeClass;
	RecordTelemetryPair(jniEnvironment, clientHandle, trackedId, engagementName, ApexTelemetryEngagementEnd);
}

static void JNICALL NativeTelemetryInteraction(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
											   jbyteArray trackedId, jbyteArray actionName)
{
	(void)bridgeClass;
	RecordTelemetryPair(jniEnvironment, clientHandle, trackedId, actionName, ApexTelemetryInteraction);
}

static void JNICALL NativeTelemetryStepBegin(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
											 jbyteArray stepName)
{
	char* nativeStepName = CopyJavaBytes(jniEnvironment, stepName, NULL);
	(void)bridgeClass;
	ApexTelemetryStepBegin(GetClient(clientHandle), nativeStepName);
	free(nativeStepName);
}

static void JNICALL NativeTelemetryStepEnd(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										   jbyteArray stepName, jboolean succeeded, jdouble score)
{
	char* nativeStepName = CopyJavaBytes(jniEnvironment, stepName, NULL);
	(void)bridgeClass;
	ApexTelemetryStepEnd(GetClient(clientHandle), nativeStepName, succeeded ? 1 : 0, score);
	free(nativeStepName);
}

static jboolean JNICALL NativeTelemetryPoseDue(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	return ApexTelemetryPoseDue(GetClient(clientHandle)) ? JNI_TRUE : JNI_FALSE;
}

static void JNICALL NativeTelemetryPose(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle,
										jbyteArray trackedId, jfloatArray javaPose)
{
	char* nativeTrackedId = CopyJavaBytes(jniEnvironment, trackedId, NULL);
	const ApexPose pose = ConvertPose(jniEnvironment, javaPose);
	(void)bridgeClass;
	ApexTelemetryPose(GetClient(clientHandle), nativeTrackedId, javaPose != NULL ? &pose : NULL);
	free(nativeTrackedId);
}

static void JNICALL NativeTelemetryFlush(JNIEnv* jniEnvironment, jclass bridgeClass, jlong clientHandle)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	ApexTelemetryFlush(GetClient(clientHandle));
}

/* ------------------------------------------------------------------------ */
/* Library-wide functions                                                    */
/* ------------------------------------------------------------------------ */

static jint JNICALL NativeAbiVersion(JNIEnv* jniEnvironment, jclass bridgeClass)
{
	(void)jniEnvironment;
	(void)bridgeClass;
	return (jint)ApexAbiVersion();
}

static jbyteArray JNICALL NativeVersionString(JNIEnv* jniEnvironment, jclass bridgeClass)
{
	(void)bridgeClass;
	return MakeJavaText(jniEnvironment, ApexVersionString());
}

static jbyteArray JNICALL NativeBuiltinTransportName(JNIEnv* jniEnvironment, jclass bridgeClass)
{
	(void)bridgeClass;
	return MakeJavaText(jniEnvironment, ApexBuiltinTransportName());
}

static jboolean JNICALL NativeModuleVersionIsValid(JNIEnv* jniEnvironment, jclass bridgeClass, jbyteArray moduleVersion)
{
	char* nativeModuleVersion = CopyJavaBytes(jniEnvironment, moduleVersion, NULL);
	const int32_t versionIsValid = ApexModuleVersionIsValid(nativeModuleVersion);
	(void)bridgeClass;
	free(nativeModuleVersion);
	return versionIsValid ? JNI_TRUE : JNI_FALSE;
}

/* ------------------------------------------------------------------------ */
/* Registration                                                              */
/* ------------------------------------------------------------------------ */

#define LISTENER_SIGNATURE "L" LISTENER_CLASS ";"

/* JNINativeMethod holds functions as void*, a conversion ISO C leaves to the
 * platform; __extension__ tells GCC's -Wpedantic that it is intended. */
#if defined(__GNUC__)
#define NATIVE_FUNCTION(nativeFunction) (__extension__(void*)(nativeFunction))
#else
#define NATIVE_FUNCTION(nativeFunction) ((void*)(nativeFunction))
#endif

static const JNINativeMethod kBridgeMethods[] = {
	{"create", "(I[B[B[BI[B[B[B[B[BDIII[BDDII" LISTENER_SIGNATURE ")J", NATIVE_FUNCTION(NativeCreate)},
	{"destroy", "(J)V", NATIVE_FUNCTION(NativeDestroy)},
	{"poll", "(JD)V", NATIVE_FUNCTION(NativePoll)},
	{"cancel", "(JJ)V", NATIVE_FUNCTION(NativeCancel)},
	{"ping", "(J)J", NATIVE_FUNCTION(NativePing)},
	{"login", "(J[B[B)J", NATIVE_FUNCTION(NativeLogin)},
	{"loginWithToken", "(J[B)J", NATIVE_FUNCTION(NativeLoginWithToken)},
	{"quickIdGetUsers", "(J[B)J", NATIVE_FUNCTION(NativeQuickIdGetUsers)},
	{"quickIdLogin", "(J[B[B)J", NATIVE_FUNCTION(NativeQuickIdLogin)},
	{"generateLoginCode", "(JI)J", NATIVE_FUNCTION(NativeGenerateLoginCode)},
	{"logout", "(J)V", NATIVE_FUNCTION(NativeLogout)},
	{"getUserModules", "(JI)J", NATIVE_FUNCTION(NativeGetUserModules)},
	{"checkModuleAccess", "(JI)J", NATIVE_FUNCTION(NativeCheckModuleAccess)},
	{"getModuleList", "(J[B)J", NATIVE_FUNCTION(NativeGetModuleList)},
	{"getModulePage", "(J[BIIIIZ)J", NATIVE_FUNCTION(NativeGetModulePage)},
	{"sessionJoin", "(J[B)J", NATIVE_FUNCTION(NativeSessionJoin)},
	{"sessionEvent", "(J[B)J", NATIVE_FUNCTION(NativeSessionEvent)},
	{"sessionComplete", "(J[B)J", NATIVE_FUNCTION(NativeSessionComplete)},
	{"isLoggedIn", "(J)Z", NATIVE_FUNCTION(NativeIsLoggedIn)},
	{"getCurrentUser", "(J)[B", NATIVE_FUNCTION(NativeGetCurrentUser)},
	{"getSessionId", "(J)J", NATIVE_FUNCTION(NativeGetSessionId)},
	{"setDeviceId", "(J[B)V", NATIVE_FUNCTION(NativeSetDeviceId)},
	{"setSessionFormat", "(JI)V", NATIVE_FUNCTION(NativeSetSessionFormat)},
	{"launchParseArgs", "(J[[B)V", NATIVE_FUNCTION(NativeLaunchParseArgs)},
	{"launchParseUrl", "(J[B)V", NATIVE_FUNCTION(NativeLaunchParseUrl)},
	{"launchSet", "(J[B[B)V", NATIVE_FUNCTION(NativeLaunchSet)},
	{"launchGet", "(J[B)[B", NATIVE_FUNCTION(NativeLaunchGet)},
	{"launchExitTarget", "(J[B)[B", NATIVE_FUNCTION(NativeLaunchExitTarget)},
	{"telemetryObjectRegistered", "(J[B[B[B[F)V", NATIVE_FUNCTION(NativeTelemetryObjectRegistered)},
	{"telemetryObjectUnregistered", "(J[B[F)V", NATIVE_FUNCTION(NativeTelemetryObjectUnregistered)},
	{"telemetryEngagementBegin", "(J[B[B)V", NATIVE_FUNCTION(NativeTelemetryEngagementBegin)},
	{"telemetryEngagementEnd", "(J[B[B)V", NATIVE_FUNCTION(NativeTelemetryEngagementEnd)},
	{"telemetryInteraction", "(J[B[B)V", NATIVE_FUNCTION(NativeTelemetryInteraction)},
	{"telemetryStepBegin", "(J[B)V", NATIVE_FUNCTION(NativeTelemetryStepBegin)},
	{"telemetryStepEnd", "(J[BZD)V", NATIVE_FUNCTION(NativeTelemetryStepEnd)},
	{"telemetryPoseDue", "(J)Z", NATIVE_FUNCTION(NativeTelemetryPoseDue)},
	{"telemetryPose", "(J[B[F)V", NATIVE_FUNCTION(NativeTelemetryPose)},
	{"telemetryFlush", "(J)V", NATIVE_FUNCTION(NativeTelemetryFlush)},
	{"abiVersion", "()I", NATIVE_FUNCTION(NativeAbiVersion)},
	{"versionString", "()[B", NATIVE_FUNCTION(NativeVersionString)},
	{"builtinTransportName", "()[B", NATIVE_FUNCTION(NativeBuiltinTransportName)},
	{"moduleVersionIsValid", "([B)Z", NATIVE_FUNCTION(NativeModuleVersionIsValid)},
};

/* Registers the natives, resolves the listener methods, and gives the core's
 * Android backend the JavaVM: the core is linked in statically, so its own
 * JNI_OnLoad does not exist here. */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* javaVirtualMachine, void* reserved)
{
	JNIEnv* jniEnvironment = NULL;
	jclass bridgeClass;
	jclass listenerClass;
	(void)reserved;

	if ((*javaVirtualMachine)->GetEnv(javaVirtualMachine, (void**)&jniEnvironment, JNI_VERSION_1_6) != JNI_OK)
	{
		return JNI_ERR;
	}

	bridgeClass = (*jniEnvironment)->FindClass(jniEnvironment, BRIDGE_CLASS);
	listenerClass = (*jniEnvironment)->FindClass(jniEnvironment, LISTENER_CLASS);
	if (bridgeClass == NULL || listenerClass == NULL)
	{
		return JNI_ERR;
	}

	listenerOnResult = (*jniEnvironment)->GetMethodID(jniEnvironment, listenerClass, "onResult", "(JII[B[B)V");
	listenerOnLog = (*jniEnvironment)->GetMethodID(jniEnvironment, listenerClass, "onLog", "(I[B)V");
	listenerOnTelemetryPacket =
		(*jniEnvironment)->GetMethodID(jniEnvironment, listenerClass, "onTelemetryPacket", "([B)V");
	if (listenerOnResult == NULL || listenerOnLog == NULL || listenerOnTelemetryPacket == NULL)
	{
		return JNI_ERR;
	}

	if ((*jniEnvironment)
			->RegisterNatives(jniEnvironment, bridgeClass, kBridgeMethods,
							  (jint)(sizeof(kBridgeMethods) / sizeof(kBridgeMethods[0]))) != JNI_OK)
	{
		return JNI_ERR;
	}

	bridgeJavaVm = javaVirtualMachine;
	ApexAndroidSetJavaVm(javaVirtualMachine);
	return JNI_VERSION_1_6;
}
