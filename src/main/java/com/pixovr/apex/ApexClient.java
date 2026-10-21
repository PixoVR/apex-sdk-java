package com.pixovr.apex;

import java.nio.charset.Charset;
import java.util.HashMap;
import java.util.Map;

/**
 * A connection to the Apex platform: login, module access and lists, sessions,
 * telemetry and launcher hand-off, through the bundled apex-sdk-core.
 *
 * <pre>
 * try (ApexClient client = new ApexClient(new ApexConfig(ApexEnvironment.NA_STAGING, 42))) {
 *     ApexResult login = client.login("ada", password, null).await(30_000).throwIfFailed();
 *     String userJson = login.getJson();
 * }
 * </pre>
 *
 * <p>The core's rules apply:
 * <ul>
 * <li><b>One thread.</b> A client belongs to the thread that created it. Call every method from that
 * thread; other threads get an IllegalStateException.</li>
 * <li><b>Results arrive on poll.</b> Every call returns an {@link ApexCall} at once and gets exactly one
 * {@link ApexResult}, delivered inside {@link #poll()}. Call poll() regularly (every frame, or from a
 * timer on the owner thread), or {@link #waitFor} a single call.</li>
 * <li><b>Callbacks</b> run on the owner thread, inside poll() and the other calls. They may call back in,
 * including {@link #close()}.</li>
 * </ul>
 */
public final class ApexClient implements AutoCloseable {
    private static final Charset UTF_8 = Charset.forName("UTF-8");
    private static final long WAIT_POLL_INTERVAL_MILLISECONDS = 10;

    /** Receives a call's result, on the owner thread. */
    public interface ResultCallback {
        void onResult(ApexResult result);
    }

    /** Receives log lines at or above ApexConfig's log level. Logs never contain bodies, tokens or passwords. */
    public interface LogListener {
        void onLog(ApexLogLevel logLevel, String message);
    }

    /** Receives telemetry packets (apex-sdk-core spec/schema/telemetry_packet.json) with the CALLBACK sink. */
    public interface TelemetryListener {
        void onTelemetryPacket(String packetJson);
    }

    private interface NativeCall {
        long run(long clientHandle);
    }

    private final Thread ownerThread = Thread.currentThread();
    private final Map<Long, ApexCall> pendingCalls = new HashMap<Long, ApexCall>();
    private final Map<Long, ApexResult> earlyResults = new HashMap<Long, ApexResult>();
    private final LogListener logListener;
    private final TelemetryListener telemetryListener;
    private long clientHandle;
    private int nativeCallDepth;
    private boolean closeRequested;

    public ApexClient(ApexConfig config) {
        this(config, null, null);
    }

    /** Either listener may be null. */
    public ApexClient(ApexConfig config, LogListener logListener, TelemetryListener telemetryListener) {
        ApexNativeLibrary.load();
        this.logListener = logListener;
        this.telemetryListener = telemetryListener;
        clientHandle = ApexNativeBridge.create(config.environment.code(), utf8(config.headsetApiUrl),
            utf8(config.platformApiUrl), utf8(config.eventPath), config.moduleId, utf8(config.moduleVersion),
            utf8(config.deviceId), utf8(config.deviceModel), utf8(config.platform), utf8(config.clientIp),
            config.heartbeatIntervalSeconds, config.requestTimeoutMilliseconds, config.logLevel.code(),
            config.telemetrySink.code(), utf8(config.telemetryUrl), config.telemetryFlushIntervalSeconds,
            config.telemetryPoseSamplesPerSecond, config.telemetryMaximumRecordsPerPacket,
            config.sessionFormat.code(), new BridgeListener());
        if (clientHandle == 0) {
            throw new IllegalArgumentException(
                "the core refused the configuration (CUSTOM needs both the Headset and Platform API URLs)");
        }
    }

    // -- lifetime ---------------------------------------------------------------

    /**
     * Destroys the client; calls still pending get no result. Called from inside a
     * callback, it happens once the outermost call into the core returns.
     */
    @Override
    public void close() {
        checkThread();
        if (clientHandle == 0) {
            return;
        }

        if (nativeCallDepth > 0) {
            closeRequested = true;
            return;
        }

        long closingHandle = clientHandle;
        clientHandle = 0;
        pendingCalls.clear();
        earlyResults.clear();
        ApexNativeBridge.destroy(closingHandle);
    }

    public boolean isClosed() {
        return clientHandle == 0 || closeRequested;
    }

    // -- polling ----------------------------------------------------------------

    /** Delivers finished results and drives the session heartbeat and telemetry. Call it regularly. */
    public void poll() {
        poll(System.nanoTime() / 1e9);
    }

    /** As poll(), with nowSeconds from any monotonic clock. */
    public void poll(final double nowSeconds) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.poll(handle, nowSeconds);
                return 0;
            }
        });
    }

    /**
     * Polls until the call's result arrives and returns it. After timeoutMilliseconds
     * the call is cancelled, and its CANCELLED result returned.
     */
    public ApexResult waitFor(ApexCall call, long timeoutMilliseconds) throws InterruptedException {
        long deadlineNanoseconds = System.nanoTime() + timeoutMilliseconds * 1000000L;
        while (!call.isDone() && !isClosed()) {
            poll();
            if (call.isDone()) {
                break;
            }

            if (System.nanoTime() - deadlineNanoseconds >= 0) {
                cancel(call);
                poll();
                break;
            }

            Thread.sleep(WAIT_POLL_INTERVAL_MILLISECONDS);
        }

        if (call.isDone()) {
            return call.getResult();
        }

        return new ApexResult(call.getCallId(), ApexErrorCode.CANCELLED.code(), 0, "the client was closed", null);
    }

    /** Its result then arrives on the next poll with CANCELLED, unless it already finished. */
    public void cancel(final ApexCall call) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.cancel(handle, call.getCallId());
                return 0;
            }
        });
    }

    // -- platform calls ---------------------------------------------------------
    // Every callback may be null; the result is also on the returned ApexCall.

    public ApexCall ping(ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.ping(handle);
            }
        });
    }

    /** Logs in and makes the user current. Payload: the user (spec/schema/user.json). */
    public ApexCall login(final String username, final String password, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.login(handle, utf8(username), utf8(password));
            }
        });
    }

    /** userToken null uses the launch argument "pixotoken". Payload: the user. */
    public ApexCall loginWithToken(final String userToken, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.loginWithToken(handle, utf8(userToken));
            }
        });
    }

    /** Users enrolled for Quick ID on a device. serialNumber null uses the configured device ID. */
    public ApexCall quickIdGetUsers(final String serialNumber, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.quickIdGetUsers(handle, utf8(serialNumber));
            }
        });
    }

    /** serialNumber null uses the configured device ID. Payload: the user. */
    public ApexCall quickIdLogin(final String serialNumber, final String username, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.quickIdLogin(handle, utf8(serialNumber), utf8(username));
            }
        });
    }

    /** One-time login code. userId &lt; 0 generates it for the current user. */
    public ApexCall generateLoginCode(final int userId, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.generateLoginCode(handle, userId);
            }
        });
    }

    /** Forgets the current user and ends any session locally. */
    public void logout() {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.logout(handle);
                return 0;
            }
        });
    }

    /** userId &lt; 0 means the current user. */
    public ApexCall getUserModules(final int userId, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.getUserModules(handle, userId);
            }
        });
    }

    /** moduleId &lt; 0 means the configured module. Uses the current user and the device ID. */
    public ApexCall checkModuleAccess(final int moduleId, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.checkModuleAccess(handle, moduleId);
            }
        });
    }

    /** Modules available to the current user, with versions built for platformName, leaving out Archived. */
    public ApexCall getModuleList(final String platformName, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.getModuleList(handle, utf8(platformName));
            }
        });
    }

    /**
     * One page of the current user's modules (spec/schema/module_page.json).
     *
     * @param platformName keep versions built for this platform; null for any
     * @param lifecycleMask {@link ApexLifecycle} flags; 0 means RELEASED
     * @param pageIndex from 0
     * @param pageSize at least 1
     * @param refresh fetch the list again instead of paging the cached one
     */
    public ApexCall getModulePage(final String platformName, final int lifecycleMask, final ApexModuleSort sortOrder,
        final int pageIndex, final int pageSize, final boolean refresh, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.getModulePage(handle, utf8(platformName), lifecycleMask, sortOrder.code(),
                    pageIndex, pageSize, refresh);
            }
        });
    }

    // -- sessions ---------------------------------------------------------------

    /**
     * Starts a session for the current user. Payload: {"sessionId": n}. With
     * ApexSessionFormat.DATA the bytes are the module's own; with XAPI they must
     * be a JSON object (see apex-sdk-core spec/README.md).
     */
    public ApexCall sessionJoin(final byte[] moduleData, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.sessionJoin(handle, moduleData);
            }
        });
    }

    /** sessionJoin with UTF-8 text, e.g. JSON. */
    public ApexCall sessionJoin(String moduleData, ResultCallback callback) {
        return sessionJoin(utf8(moduleData), callback);
    }

    public ApexCall sessionEvent(final byte[] moduleData, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.sessionEvent(handle, moduleData);
            }
        });
    }

    public ApexCall sessionEvent(String moduleData, ResultCallback callback) {
        return sessionEvent(utf8(moduleData), callback);
    }

    /** Ends the session when the platform accepts it; on failure it stays active so it can be retried. */
    public ApexCall sessionComplete(final byte[] moduleData, ResultCallback callback) {
        return startCall(callback, new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.sessionComplete(handle, moduleData);
            }
        });
    }

    public ApexCall sessionComplete(String moduleData, ResultCallback callback) {
        return sessionComplete(utf8(moduleData), callback);
    }

    // -- state ------------------------------------------------------------------

    public boolean isLoggedIn() {
        return callNative(new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.isLoggedIn(handle) ? 1 : 0;
            }
        }) != 0;
    }

    /** The current user as JSON (spec/schema/user.json), or "" when logged out. */
    public String getCurrentUser() {
        final String[] userJson = {""};
        callNative(new NativeCall() {
            public long run(long handle) {
                userJson[0] = text(ApexNativeBridge.getCurrentUser(handle));
                return 0;
            }
        });
        return userJson[0];
    }

    /** 0 when no session is active. */
    public long getSessionId() {
        return callNative(new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.getSessionId(handle);
            }
        });
    }

    /** Replaces the configured device ID, e.g. once a device manager reports the serial number. */
    public void setDeviceId(final String deviceId) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.setDeviceId(handle, utf8(deviceId));
                return 0;
            }
        });
    }

    /** Applies from the next join; an active session keeps its format. */
    public void setSessionFormat(final ApexSessionFormat sessionFormat) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.setSessionFormat(handle, sessionFormat.code());
                return 0;
            }
        });
    }

    // -- launch arguments and the launcher ---------------------------------------

    /** Command-line arguments with the executable first, as the OS passes them. */
    public void launchParseArgs(String[] argumentValues) {
        final byte[][] encodedArguments = new byte[argumentValues.length][];
        for (int argumentIndex = 0; argumentIndex < argumentValues.length; argumentIndex++) {
            encodedArguments[argumentIndex] = utf8(argumentValues[argumentIndex]);
        }

        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.launchParseArgs(handle, encodedArguments);
                return 0;
            }
        });
    }

    /** Launcher URL data, e.g. an Android intent's data string. */
    public void launchParseUrl(final String launchUrl) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.launchParseUrl(handle, utf8(launchUrl));
                return 0;
            }
        });
    }

    /** Sets one launch argument, e.g. an Android intent extra. */
    public void launchSet(final String key, final String launchValue) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.launchSet(handle, utf8(key), utf8(launchValue));
                return 0;
            }
        });
    }

    /** "" when the argument is not set. */
    public String launchGet(final String key) {
        final String[] launchValue = {""};
        callNative(new NativeCall() {
            public long run(long handle) {
                launchValue[0] = text(ApexNativeBridge.launchGet(handle, utf8(key)));
                return 0;
            }
        });
        return launchValue[0];
    }

    /**
     * How to hand control back to the launcher, as JSON: {"action":"none"},
     * {"action":"open_url","url":...} or {"action":"open_app","package":...,"extras":{...}}.
     * nextTarget (may be null) is passed on to the launcher as "returntarget".
     */
    public String launchExitTarget(final String nextTarget) {
        final String[] exitTarget = {""};
        callNative(new NativeCall() {
            public long run(long handle) {
                exitTarget[0] = text(ApexNativeBridge.launchExitTarget(handle, utf8(nextTarget)));
                return 0;
            }
        });
        return exitTarget[0];
    }

    // -- telemetry --------------------------------------------------------------
    // Objects are named by a tracked ID the app chooses and keeps stable. Poses may be null.

    public void telemetryObjectRegistered(final String trackedId, final String objectName, final String meshName,
        final ApexPose pose) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.telemetryObjectRegistered(handle, utf8(trackedId), utf8(objectName), utf8(meshName),
                    ApexPose.toArray(pose));
                return 0;
            }
        });
    }

    public void telemetryObjectUnregistered(final String trackedId, final ApexPose pose) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.telemetryObjectUnregistered(handle, utf8(trackedId), ApexPose.toArray(pose));
                return 0;
            }
        });
    }

    public void telemetryEngagementBegin(final String trackedId, final String engagementName) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.telemetryEngagementBegin(handle, utf8(trackedId), utf8(engagementName));
                return 0;
            }
        });
    }

    public void telemetryEngagementEnd(final String trackedId, final String engagementName) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.telemetryEngagementEnd(handle, utf8(trackedId), utf8(engagementName));
                return 0;
            }
        });
    }

    public void telemetryInteraction(final String trackedId, final String actionName) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.telemetryInteraction(handle, utf8(trackedId), utf8(actionName));
                return 0;
            }
        });
    }

    public void telemetryStepBegin(final String stepName) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.telemetryStepBegin(handle, utf8(stepName));
                return 0;
            }
        });
    }

    public void telemetryStepEnd(final String stepName, final boolean succeeded, final double score) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.telemetryStepEnd(handle, utf8(stepName), succeeded, score);
                return 0;
            }
        });
    }

    /** True at most telemetryPoseSamplesPerSecond times a second; then report the head and every object. */
    public boolean telemetryPoseDue() {
        return callNative(new NativeCall() {
            public long run(long handle) {
                return ApexNativeBridge.telemetryPoseDue(handle) ? 1 : 0;
            }
        }) != 0;
    }

    /** trackedId null is the head. Unchanged poses are dropped. */
    public void telemetryPose(final String trackedId, final ApexPose pose) {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.telemetryPose(handle, utf8(trackedId), ApexPose.toArray(pose));
                return 0;
            }
        });
    }

    /** Sends buffered records now, e.g. when the app is paused. */
    public void telemetryFlush() {
        callNative(new NativeCall() {
            public long run(long handle) {
                ApexNativeBridge.telemetryFlush(handle);
                return 0;
            }
        });
    }

    // -- library-wide -----------------------------------------------------------

    /** The bundled core's version, e.g. "0.2.0". */
    public static String getCoreVersion() {
        ApexNativeLibrary.load();
        return text(ApexNativeBridge.versionString());
    }

    /** The bundled core's ABI as (major &lt;&lt; 16) | minor. */
    public static int getCoreAbiVersion() {
        ApexNativeLibrary.load();
        return ApexNativeBridge.abiVersion();
    }

    /**
     * True for "MAJOR.MINOR.PATCH" with a major that does not start with 0 and
     * one- or two-digit minor and patch.
     */
    public static boolean isModuleVersionValid(String moduleVersion) {
        ApexNativeLibrary.load();
        return ApexNativeBridge.moduleVersionIsValid(utf8(moduleVersion));
    }

    // -- plumbing ---------------------------------------------------------------

    static byte[] utf8(String text) {
        return text != null ? text.getBytes(UTF_8) : null;
    }

    static String text(byte[] utf8Bytes) {
        return utf8Bytes != null ? new String(utf8Bytes, UTF_8) : null;
    }

    private void checkThread() {
        if (Thread.currentThread() != ownerThread) {
            throw new IllegalStateException("an ApexClient must be used from the thread that created it");
        }
    }

    private long callNative(NativeCall nativeCall) {
        checkThread();
        if (clientHandle == 0 || closeRequested) {
            throw new IllegalStateException("the client is closed");
        }

        nativeCallDepth++;
        try {
            return nativeCall.run(clientHandle);
        } finally {
            nativeCallDepth--;
            if (nativeCallDepth == 0 && closeRequested) {
                closeRequested = false;
                close();
            }
        }
    }

    private ApexCall startCall(ResultCallback callback, NativeCall nativeCall) {
        long callId = callNative(nativeCall);
        ApexCall call = new ApexCall(this, callId, callback);
        ApexResult earlyResult = earlyResults.remove(callId);
        if (earlyResult != null) {
            call.finish(earlyResult);
        } else {
            pendingCalls.put(callId, call);
        }

        return call;
    }

    private final class BridgeListener implements ApexNativeBridge.Listener {
        @Override
        public void onResult(long callId, int error, int httpStatus, byte[] message, byte[] json) {
            if (closeRequested) {
                return;
            }

            ApexResult result = new ApexResult(callId, error, httpStatus, text(message), text(json));
            ApexCall call = pendingCalls.remove(callId);
            if (call == null) {
                earlyResults.put(callId, result);
                return;
            }

            // An exception must not unwind through the core; report it and carry on polling.
            try {
                call.finish(result);
            } catch (RuntimeException callbackError) {
                reportCallbackError(callbackError);
            }
        }

        @Override
        public void onLog(int logLevel, byte[] message) {
            if (logListener == null) {
                return;
            }

            try {
                ApexLogLevel level = ApexLogLevel.fromCode(logLevel);
                logListener.onLog(level != null ? level : ApexLogLevel.ERROR, text(message));
            } catch (RuntimeException listenerError) {
                reportCallbackError(listenerError);
            }
        }

        @Override
        public void onTelemetryPacket(byte[] packetJson) {
            if (telemetryListener == null) {
                return;
            }

            try {
                telemetryListener.onTelemetryPacket(text(packetJson));
            } catch (RuntimeException listenerError) {
                reportCallbackError(listenerError);
            }
        }

        private void reportCallbackError(RuntimeException callbackError) {
            callbackError.printStackTrace();
        }
    }
}
