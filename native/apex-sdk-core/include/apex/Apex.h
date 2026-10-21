/*
 * Apex.h - C interface to the Apex platform core.
 *
 * Threading
 *   A client is owned by one thread (the engine's game thread). Every function
 *   below must be called from that thread, except ApexHttpComplete, which may
 *   be called from any thread. Result callbacks run only inside ApexClientPoll,
 *   on the owner thread.
 *
 * Strings
 *   All strings are UTF-8 and NUL-terminated. Strings passed in are copied before
 *   the call returns. Strings passed out (in ApexResult and ApexLogCallback) are
 *   valid only for the duration of the callback; copy what you keep.
 *
 * Results
 *   Every call that returns an ApexCallId delivers exactly one ApexResult,
 *   including when it fails immediately (bad arguments, not logged in). Payloads
 *   are normalized JSON documents described in spec/schema.
 *
 * Versioning
 *   From ABI 1.0 on, changes within one major version are additive only;
 *   0.x minor versions may still break. Structs that may grow carry a
 *   StructSize field; always initialise them with their Init function.
 *
 * Naming
 *   Functions and types are PascalCase with an Apex prefix, enums are EApex...
 *   with UPPER_CASE values, and struct fields are PascalCase (see AGENTS.md).
 */
#ifndef APEX_APEX_H
#define APEX_APEX_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(APEX_BUILD_SHARED)
#define APEX_API __declspec(dllexport)
#elif defined(APEX_USE_SHARED)
#define APEX_API __declspec(dllimport)
#else
#define APEX_API
#endif
#define APEX_CALL __cdecl
#else
#if defined(APEX_BUILD_SHARED)
#define APEX_API __attribute__((visibility("default")))
#else
#define APEX_API
#endif
#define APEX_CALL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define APEX_ABI_MAJOR 0
#define APEX_ABI_MINOR 7

/* ------------------------------------------------------------------------ */
/* Basic types                                                               */
/* ------------------------------------------------------------------------ */

typedef struct ApexClient ApexClient;

/* Identifies one call. 0 is never a valid id. */
typedef uint64_t ApexCallId;

typedef enum EApexError
{
	APEX_OK = 0,
	APEX_ERR_NETWORK = 1,       /* no HTTP response: DNS, TLS, timeout, offline */
	APEX_ERR_HTTP = 2,          /* HTTP status outside 2xx with no platform error body */
	APEX_ERR_PARSE = 3,         /* response body was not what the platform should send */
	APEX_ERR_PLATFORM = 4,      /* the platform reported an error; see message */
	APEX_ERR_NOT_LOGGED_IN = 5, /* the call needs a logged-in user */
	APEX_ERR_CANCELLED = 6,     /* ApexCancel was called */
	APEX_ERR_INVALID_ARGUMENT = 7,
	APEX_ERR_INVALID_STATE = 8, /* e.g. an event sent with no active session */
	APEX_ERR_NO_TRANSPORT = 9,  /* no built-in backend on this platform and no override */
	APEX_ERR_INTERNAL = 10
} EApexError;

/* Each environment has two hosts:
 *   Headset API   https://modules.apex.pixovr.com   login, users, access, session events
 *   Platform API  https://apex.pixovr.com           GraphQL, token and Quick ID login, heartbeat
 * Staging and development insert ".stage" / ".dev" after "apex"; Saudi
 * production prefixes "saudi.". APEX_ENV_LOCAL is http://localhost:8001 and
 * :8000. HeadsetApiUrl and PlatformApiUrl override either host in any
 * environment; APEX_ENV_CUSTOM requires both. */
typedef enum EApexEnvironment
{
	APEX_ENV_NA_PRODUCTION = 0,
	APEX_ENV_NA_STAGING = 1,
	APEX_ENV_NA_DEVELOPMENT = 2,
	APEX_ENV_SA_PRODUCTION = 3,
	APEX_ENV_LOCAL = 4,
	APEX_ENV_CUSTOM = 5 /* hosts come only from HeadsetApiUrl and PlatformApiUrl */
} EApexEnvironment;

typedef enum EApexLogLevel
{
	APEX_LOG_DEBUG = 0,
	APEX_LOG_INFO = 1,
	APEX_LOG_WARNING = 2,
	APEX_LOG_ERROR = 3
} EApexLogLevel;

/* Module version lifecycles, as bit flags for ApexGetModulePage. A lifecycle
 * the platform names anything other than Released, Development, QA, UAT or
 * Archived (or no lifecycle at all) counts as OTHER. Archived versions are
 * never returned. */
typedef enum EApexLifecycle
{
	APEX_LIFECYCLE_RELEASED = 1,
	APEX_LIFECYCLE_DEVELOPMENT = 2,
	APEX_LIFECYCLE_QA = 4,
	APEX_LIFECYCLE_UAT = 8,
	APEX_LIFECYCLE_OTHER = 16
} EApexLifecycle;

/* How session calls reach the platform (see Sessions below). */
typedef enum EApexSessionFormat
{
	APEX_SESSION_FORMAT_DATA = 0, /* {"uuid", "eventType", "moduleId", "data": base64 of the module's bytes} */
	APEX_SESSION_FORMAT_XAPI = 1  /* xAPI statements, as the Unity SDK 1.x sent them */
} EApexSessionFormat;

/* Order of the modules ApexGetModulePage pages through. */
typedef enum EApexModuleSort
{
	APEX_MODULE_SORT_PLATFORM = 0,      /* the order the platform returns */
	APEX_MODULE_SORT_NAME = 1,          /* name A-Z, ignoring case */
	APEX_MODULE_SORT_ID = 2,            /* module ID, lowest first */
	APEX_MODULE_SORT_NEWEST_VERSION = 3 /* newest matching version (createdAt) first */
} EApexModuleSort;

typedef struct ApexResult
{
	ApexCallId CallId;
	int32_t Error;       /* EApexError */
	int32_t HttpStatus;  /* 0 when no HTTP response was received */
	const char* Message; /* never NULL; empty on success */
	const char* Json;    /* normalized payload on success, NULL on failure */
} ApexResult;

/* userData is the pointer passed with the call or set in ApexConfig. */
typedef void(APEX_CALL* ApexResultCallback)(void* userData, const ApexResult* result);
typedef void(APEX_CALL* ApexLogCallback)(void* userData, int32_t logLevel, const char* message);

/* ------------------------------------------------------------------------ */
/* HTTP override (optional)                                                  */
/*                                                                           */
/* By default the core sends requests through the platform's own HTTP API.  */
/* Set ApexConfig.HttpSendCallback to send them yourself instead; report   */
/* each one back with ApexHttpComplete. The callback must not destroy the    */
/* client, and no ApexHttpComplete may arrive after ApexClientDestroy has    */
/* returned.                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct ApexHttpHeader
{
	const char* Name;
	const char* Value;
} ApexHttpHeader;

typedef struct ApexHttpRequest
{
	uint64_t RequestId;
	const char* Method; /* "GET" or "POST" */
	const char* Url;
	const ApexHttpHeader* Headers;
	size_t HeaderCount;
	const char* Body; /* may be NULL when BodyLength is 0 */
	size_t BodyLength;
	uint32_t TimeoutMilliseconds;
} ApexHttpRequest;

typedef void(APEX_CALL* ApexHttpSendCallback)(void* userData, const ApexHttpRequest* request);

/* ------------------------------------------------------------------------ */
/* Telemetry output                                                          */
/* ------------------------------------------------------------------------ */

typedef enum EApexTelemetrySink
{
	APEX_TELEMETRY_OFF = 0,     /* records are dropped (default) */
	APEX_TELEMETRY_HTTP = 1,    /* POST each packet to TelemetryUrl with the user's token */
	APEX_TELEMETRY_CALLBACK = 2 /* hand each packet to TelemetryCallback, e.g. to log or save it */
} EApexTelemetrySink;

/* Receives one packet as JSON (spec/schema/telemetry_packet.json), on the
 * owner thread, from inside a telemetry call, a session call or
 * ApexClientPoll. It must not destroy the client. */
typedef void(APEX_CALL* ApexTelemetryCallback)(void* userData, const char* packetJson);

/* ------------------------------------------------------------------------ */
/* Client                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct ApexConfig
{
	uint32_t StructSize; /* set by ApexConfigInit */

	int32_t Environment;        /* EApexEnvironment */
	const char* HeadsetApiUrl;  /* overrides the environment's Headset API host when set */
	const char* PlatformApiUrl; /* overrides the environment's Platform API host when set */
	const char* EventPath;      /* session event route on the Headset API; default "/event" */

	int32_t ModuleId;          /* this module's platform ID */
	const char* ModuleVersion; /* e.g. "1.4.0" */

	const char* DeviceId; /* serial number; used for Quick ID and module access */
	/* Not sent by any call yet; kept in case session events need device details. */
	const char* DeviceModel;
	const char* Platform; /* e.g. "android", "windows" */
	const char* ClientIp; /* optional */

	double HeartbeatIntervalSeconds;     /* default 5 */
	uint32_t RequestTimeoutMilliseconds; /* default 30000 */

	ApexLogCallback LogCallback; /* optional */
	void* LogUserData;
	int32_t LogLevel; /* minimum level passed to LogCallback; default APEX_LOG_INFO */

	ApexHttpSendCallback HttpSendCallback; /* optional override; NULL uses the built-in backend */
	void* HttpSendUserData;

	/* Added in ABI 0.2. */
	int32_t TelemetrySink;    /* EApexTelemetrySink */
	const char* TelemetryUrl; /* absolute URL, for APEX_TELEMETRY_HTTP */
	ApexTelemetryCallback TelemetryCallback;
	void* TelemetryUserData;
	double TelemetryFlushIntervalSeconds;     /* while a session is active; 0 means the default, 10 */
	double TelemetryPoseSamplesPerSecond;     /* 0 means the default, 2 */
	int32_t TelemetryMaximumRecordsPerPacket; /* 0 means the default, 500 */

	/* Added in ABI 0.7. */
	int32_t SessionFormat; /* EApexSessionFormat; default APEX_SESSION_FORMAT_DATA */
} ApexConfig;

/* ABI version: (major << 16) | minor. */
APEX_API uint32_t APEX_CALL ApexAbiVersion(void);
/* Library version string, e.g. "0.2.0". */
APEX_API const char* APEX_CALL ApexVersionString(void);
/* Name of the built-in HTTP backend compiled in: "winhttp", "android", or "none". */
APEX_API const char* APEX_CALL ApexBuiltinTransportName(void);

APEX_API void APEX_CALL ApexConfigInit(ApexConfig* config);

/* Returns NULL if config is NULL or the environment is unknown. A config
 * from an older ABI (smaller StructSize) is accepted; its missing fields
 * take their defaults. */
APEX_API ApexClient* APEX_CALL ApexClientCreate(const ApexConfig* config);
/* Pending calls are dropped without a result. Called from inside a result
 * callback, the client is deleted when ApexClientPoll returns and no further
 * results are delivered. */
APEX_API void APEX_CALL ApexClientDestroy(ApexClient* client);

/* Delivers finished results and drives the session heartbeat. Call once per
 * frame from the owner thread. nowSeconds is any monotonic clock. */
APEX_API void APEX_CALL ApexClientPoll(ApexClient* client, double nowSeconds);

/* Cancels a call. Its result is delivered on the next poll with
 * APEX_ERR_CANCELLED, unless it already finished. */
APEX_API void APEX_CALL ApexCancel(ApexClient* client, ApexCallId callId);

/* Reports an overridden HTTP request's outcome. Thread-safe. Pass
 * transportError (non-NULL, non-empty) when no HTTP response was received. */
APEX_API void APEX_CALL ApexHttpComplete(ApexClient* client, uint64_t requestId, int32_t httpStatus,
										 const char* responseBody, size_t responseBodyLength,
										 const char* transportError);

/* ------------------------------------------------------------------------ */
/* Platform calls                                                            */
/* ------------------------------------------------------------------------ */

APEX_API ApexCallId APEX_CALL ApexPing(ApexClient* client, ApexResultCallback onResult, void* userData);

/* Logs in and makes the user current. Payload: user (schema/user.json). */
APEX_API ApexCallId APEX_CALL ApexLogin(ApexClient* client, const char* username, const char* password,
										ApexResultCallback onResult, void* userData);
/* userToken NULL or empty uses the launch argument "pixotoken". Payload: user. */
APEX_API ApexCallId APEX_CALL ApexLoginWithToken(ApexClient* client, const char* userToken, ApexResultCallback onResult,
												 void* userData);
/* Users enrolled for Quick ID on a device. serialNumber NULL uses DeviceId. */
APEX_API ApexCallId APEX_CALL ApexQuickIdGetUsers(ApexClient* client, const char* serialNumber,
												  ApexResultCallback onResult, void* userData);
/* Payload: user. serialNumber NULL uses DeviceId. */
APEX_API ApexCallId APEX_CALL ApexQuickIdLogin(ApexClient* client, const char* serialNumber, const char* username,
											   ApexResultCallback onResult, void* userData);
/* One-time login code. userId < 0 generates it for the current user. */
APEX_API ApexCallId APEX_CALL ApexGenerateLoginCode(ApexClient* client, int32_t userId, ApexResultCallback onResult,
													void* userData);
/* Forgets the current user and ends any session locally. No result. */
APEX_API void APEX_CALL ApexLogout(ApexClient* client);

/* userId < 0 means the current user. */
APEX_API ApexCallId APEX_CALL ApexGetUserModules(ApexClient* client, int32_t userId, ApexResultCallback onResult,
												 void* userData);
/* moduleId < 0 means ModuleId. Uses the current user and DeviceId. */
APEX_API ApexCallId APEX_CALL ApexCheckModuleAccess(ApexClient* client, int32_t moduleId, ApexResultCallback onResult,
													void* userData);
/* Modules available to the current user, keeping only versions built for
 * platformName (matched on platform name or short name, case-insensitive) and
 * leaving out Archived versions. Always asks the platform, and refreshes the
 * list ApexGetModulePage pages through. */
APEX_API ApexCallId APEX_CALL ApexGetModuleList(ApexClient* client, const char* platformName,
												ApexResultCallback onResult, void* userData);
/* One page of the current user's modules (spec/schema/module_page.json).
 * Keeps versions built for platformName (NULL or empty: any platform) whose
 * lifecycle is in lifecycleMask (EApexLifecycle flags; 0 means RELEASED), and
 * modules with at least one such version. sortOrder is an EApexModuleSort.
 * pageIndex starts at 0 and pageSize must be at least 1; a page past the end
 * is empty. The first call fetches the whole list once and keeps it until
 * logout, the next login, ApexGetModuleList, or a call with refresh != 0;
 * pages served from it complete on the next poll with HttpStatus 0. */
APEX_API ApexCallId APEX_CALL ApexGetModulePage(ApexClient* client, const char* platformName, uint32_t lifecycleMask,
												int32_t sortOrder, int32_t pageIndex, int32_t pageSize, int32_t refresh,
												ApexResultCallback onResult, void* userData);

/* ------------------------------------------------------------------------ */
/* Sessions                                                                  */
/* ------------------------------------------------------------------------ */

/* Each call POSTs to the Headset API's /event route. uuid is chosen at join
 * and repeated on every event of the session.
 *
 * APEX_SESSION_FORMAT_DATA (spec/schema/session_event.json): {"uuid",
 * "eventType", "data"}, plus "moduleId" on the join. data is whatever bytes
 * the module passes, sent base64-encoded and left out when moduleDataLength
 * is 0. The core does not look inside it.
 *
 * APEX_SESSION_FORMAT_XAPI (spec/schema/xapi_session_event.json): {"uuid",
 * "eventType", "moduleId", "deviceId", "jsonData"} with an xAPI statement in
 * jsonData, as the Unity SDK 1.x sent it. moduleData must be a JSON object:
 * the SDK's session JSON (spec/README.md), from which the core builds the
 * statement, or a statement with a "verb", which gets the actor, the session
 * context and the standard extensions. Anything else fails with
 * APEX_ERR_INVALID_ARGUMENT. */

/* Starts a session for the current user. Payload: {"sessionId": n}. */
APEX_API ApexCallId APEX_CALL ApexSessionJoin(ApexClient* client, const void* moduleData, size_t moduleDataLength,
											  ApexResultCallback onResult, void* userData);
APEX_API ApexCallId APEX_CALL ApexSessionEvent(ApexClient* client, const void* moduleData, size_t moduleDataLength,
											   ApexResultCallback onResult, void* userData);
/* Ends the session when the platform accepts it; on failure the session stays
 * active so the module can retry. */
APEX_API ApexCallId APEX_CALL ApexSessionComplete(ApexClient* client, const void* moduleData, size_t moduleDataLength,
												  ApexResultCallback onResult, void* userData);

/* ------------------------------------------------------------------------ */
/* State                                                                     */
/*                                                                           */
/* Functions that fill a buffer return the length the full value needs,     */
/* excluding the terminating NUL. Pass buffer NULL / bufferSize 0 to query   */
/* it. The buffer is always NUL-terminated when bufferSize > 0.              */
/* ------------------------------------------------------------------------ */

APEX_API int32_t APEX_CALL ApexIsLoggedIn(const ApexClient* client);
/* The current user as JSON (schema/user.json), or "" when logged out. */
APEX_API size_t APEX_CALL ApexGetCurrentUser(const ApexClient* client, char* buffer, size_t bufferSize);
/* 0 when no session is active. */
APEX_API int64_t APEX_CALL ApexGetSessionId(const ApexClient* client);
/* Replaces ApexConfig.DeviceId, the serial number Quick ID and module access
 * use, e.g. once a device manager reports it after startup. Added in ABI 0.6. */
APEX_API void APEX_CALL ApexSetDeviceId(ApexClient* client, const char* deviceId);
/* Replaces ApexConfig.SessionFormat (an EApexSessionFormat) from the next
 * join on; an active session keeps the format it started with. An unknown
 * format is ignored. Added in ABI 0.7. */
APEX_API void APEX_CALL ApexSetSessionFormat(ApexClient* client, int32_t sessionFormat);

/* ------------------------------------------------------------------------ */
/* Launch arguments and returning to the launcher                            */
/* ------------------------------------------------------------------------ */

/* Command-line arguments as the OS passes them (argumentValues[0] is the
 * executable). A single argument is treated as launcher URL data, otherwise
 * "-key value" pairs are read. */
APEX_API void APEX_CALL ApexLaunchParseArgs(ApexClient* client, int32_t argumentCount,
											const char* const* argumentValues);
/* Launcher URL data, e.g. an Android intent's data string. As in the Unity
 * SDK 2.0, the whole text is percent-decoded before it is split, so values
 * cannot carry an encoded '&' or '='. */
APEX_API void APEX_CALL ApexLaunchParseUrl(ApexClient* client, const char* launchUrl);
/* Sets one launch argument, e.g. an Android intent extra. */
APEX_API void APEX_CALL ApexLaunchSet(ApexClient* client, const char* key, const char* value);
APEX_API size_t APEX_CALL ApexLaunchGet(const ApexClient* client, const char* key, char* buffer, size_t bufferSize);

/* What the engine should do to hand control back to the launcher, as JSON:
 *   {"action":"none"}                                      just quit
 *   {"action":"open_url","url":"..."}                      open the URL, then quit
 *   {"action":"open_app","package":"...","extras":{...}}   launch the app with these extras, then quit
 * nextTarget (optional) is passed on to the launcher as "returntarget". */
APEX_API size_t APEX_CALL ApexLaunchExitTarget(const ApexClient* client, const char* nextTarget, char* buffer,
											   size_t bufferSize);

/* 1 when moduleVersion is "MAJOR.MINOR.PATCH" with a major that does not start
 * with 0 and a one- or two-digit minor and patch. */
APEX_API int32_t APEX_CALL ApexModuleVersionIsValid(const char* moduleVersion);

/* ------------------------------------------------------------------------ */
/* Telemetry                                                                 */
/*                                                                           */
/* The engine's analytics components report what happens; the core batches  */
/* it into packets and sends them per ApexConfig.TelemetrySink. Packets go */
/* out every TelemetryFlushIntervalSeconds while a session is active, when */
/* a packet reaches TelemetryMaximumRecordsPerPacket, on                   */
/* ApexTelemetryFlush, and when the session ends (that packet is marked      */
/* "end").                                                                   */
/*                                                                           */
/* Objects are named by a tracked ID the engine chooses and keeps stable.    */
/* Poses use meters and Euler degrees in Unity's axes (x right, y up,        */
/* z forward); other engines convert before calling.                         */
/* ------------------------------------------------------------------------ */

typedef struct ApexPose
{
	float Position[3];
	float Rotation[3];
} ApexPose;

APEX_API void APEX_CALL ApexTelemetryObjectRegistered(ApexClient* client, const char* trackedId, const char* objectName,
													  const char* meshName, const ApexPose* pose);
APEX_API void APEX_CALL ApexTelemetryObjectUnregistered(ApexClient* client, const char* trackedId,
														const ApexPose* pose);
APEX_API void APEX_CALL ApexTelemetryEngagementBegin(ApexClient* client, const char* trackedId,
													 const char* engagementName);
APEX_API void APEX_CALL ApexTelemetryEngagementEnd(ApexClient* client, const char* trackedId,
												   const char* engagementName);
APEX_API void APEX_CALL ApexTelemetryInteraction(ApexClient* client, const char* trackedId, const char* actionName);
APEX_API void APEX_CALL ApexTelemetryStepBegin(ApexClient* client, const char* stepName);
APEX_API void APEX_CALL ApexTelemetryStepEnd(ApexClient* client, const char* stepName, int32_t succeeded, double score);
/* 1 at most TelemetryPoseSamplesPerSecond times a second. When it returns 1,
 * report the head and every tracked object's pose with ApexTelemetryPose. */
APEX_API int32_t APEX_CALL ApexTelemetryPoseDue(ApexClient* client);
/* trackedId NULL or empty is the head. Unchanged poses are dropped. */
APEX_API void APEX_CALL ApexTelemetryPose(ApexClient* client, const char* trackedId, const ApexPose* pose);
/* Sends buffered records now, e.g. when the app is paused. */
APEX_API void APEX_CALL ApexTelemetryFlush(ApexClient* client);

/* ------------------------------------------------------------------------ */
/* Android                                                                   */
/* ------------------------------------------------------------------------ */

/* Gives the built-in Android backend the process's JavaVM (a JavaVM*). Not
 * needed when the core is loaded as its own shared library: its JNI_OnLoad
 * captures the VM. Call it when the core is linked statically into another
 * library, e.g. an Unreal game. Must be called from a thread attached to the
 * JVM, before the first request. */
APEX_API void APEX_CALL ApexAndroidSetJavaVm(void* javaVirtualMachine);

#ifdef __cplusplus
}
#endif

#endif /* APEX_APEX_H */
