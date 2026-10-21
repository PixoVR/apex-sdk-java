package com.pixovr.apex;

import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

/**
 * Runs the smoke test's calls through ApexClient, the JNI bridge and the core's
 * java.net backend. Gradle's smokeFake tasks start it under the core's fake
 * platform, which appends the apex_smoke options and checks every request:
 *
 * <pre>
 * fake_apex.py run -- java -cp CLASSPATH com.pixovr.apex.SmokeTest
 * </pre>
 *
 * Exits 0 when every step passed.
 */
public final class SmokeTest {
    private static final double CALL_TIMEOUT_SECONDS = 10.0;
    private static final long POLL_INTERVAL_MILLISECONDS = 10;

    private final Map<String, String> options;
    private ApexClient client;
    private int failedStepCount;
    private int passedStepCount;
    private int heartbeatCount;
    private int telemetryUploadCount;

    private SmokeTest(Map<String, String> options) {
        this.options = options;
    }

    public static void main(String[] arguments) throws InterruptedException {
        Map<String, String> options = new HashMap<>();
        for (int argumentIndex = 0; argumentIndex < arguments.length; argumentIndex++) {
            String optionName = arguments[argumentIndex];
            boolean hasValue = argumentIndex + 1 < arguments.length && !arguments[argumentIndex + 1].startsWith("--");
            options.put(optionName, hasValue ? arguments[++argumentIndex] : "");
        }

        System.exit(new SmokeTest(options).run());
    }

    private int run() throws InterruptedException {
        boolean useXapi = options.containsKey("--xapi");
        ApexConfig config = new ApexConfig(ApexEnvironment.CUSTOM, Integer.parseInt(options.get("--module-id")))
            .setHeadsetApiUrl(options.get("--headset-url"))
            .setPlatformApiUrl(options.get("--platform-url"))
            .setModuleVersion("1.0.0")
            .setDeviceId("APEX-SMOKE")
            .setLogLevel(ApexLogLevel.DEBUG) // to count heartbeats and telemetry uploads
            .setTelemetrySink(ApexTelemetrySink.HTTP)
            .setTelemetryUrl(options.get("--telemetry-url"))
            .setSessionFormat(useXapi ? ApexSessionFormat.XAPI : ApexSessionFormat.DATA);

        System.out.println("apex-sdk-java smoke: core " + ApexClient.getCoreVersion() + ", library from "
            + ApexNativeLibrary.getLoadedFrom());
        client = new ApexClient(config, (logLevel, message) -> {
            boolean requestSucceeded = logLevel == ApexLogLevel.DEBUG;
            if (message.contains("/heartbeat/pulse -> ") && requestSucceeded) {
                heartbeatCount++;
            } else if (message.startsWith("POST /telemetry -> ") && requestSucceeded) {
                telemetryUploadCount++;
            } else if (logLevel.code() >= ApexLogLevel.WARNING.code()) {
                System.out.println("    [" + logLevel + "] " + message);
            }
        }, null);

        check("utf-8 launch value", roundTripsLaunchValue("ü-✓-😀"));
        report("ping", client::ping);
        if (!report("login", callback -> client.login(options.get("--user"), options.get("--password"), callback))) {
            client.close();
            return 1;
        }

        check("current user", client.getCurrentUser().contains("\"id\""));
        report("get user modules", callback -> client.getUserModules(-1, callback));
        report("module list", callback -> client.getModuleList(options.get("--platform"), callback));
        report("module page",
            callback -> client.getModulePage(options.get("--platform"), ApexLifecycle.ALL, ApexModuleSort.NAME, 0, 5,
                true, callback));
        report("module access", callback -> client.checkModuleAccess(-1, callback));
        report("generate login code", callback -> client.generateLoginCode(-1, callback));
        report("quick id users", callback -> client.quickIdGetUsers(null, callback));
        report("quick id login", callback -> client.quickIdLogin(null, options.get("--quick-id-user"), callback));
        runSession(useXapi);
        client.logout();
        client.close();
        System.out.println("\n" + passedStepCount + " passed, " + failedStepCount + " failed");
        return failedStepCount == 0 ? 0 : 1;
    }

    private void runSession(boolean useXapi) throws InterruptedException {
        String smokeData = options.get("--data");
        String joinData = useXapi
            ? "{\"scenarioId\":\"ApexSmoke\",\"platform\":\"windows\",\"deviceId\":\"APEX-SMOKE\","
                + "\"data\":{\"smoke\":\"" + smokeData + "\"}}"
            : smokeData;
        String eventData = useXapi
            ? "{\"scenarioId\":\"ApexSmoke\",\"action\":\"Smoke Event\",\"data\":{\"smoke\":\"" + smokeData + "\"}}"
            : smokeData;
        String completeData = useXapi
            ? "{\"scenarioId\":\"ApexSmoke\",\"score\":1,\"complete\":true,\"success\":true,"
                + "\"data\":{\"smoke\":\"" + smokeData + "\"}}"
            : smokeData;

        if (!report("session join", callback -> client.sessionJoin(utf8(joinData), callback))) {
            return;
        }

        check("session id", client.getSessionId() > 0);
        ApexPose leverPose = ApexPose.atPosition(0.5f, 1.0f, 1.2f);
        client.telemetryObjectRegistered("smoke-lever", "Lever", "LeverMesh", leverPose);
        client.telemetryStepBegin("Smoke step");
        client.telemetryInteraction("smoke-lever", "pull");
        client.telemetryPose(null, new ApexPose(0.0f, 1.6f, 0.0f, 0.0f, 90.0f, 0.0f));
        client.telemetryStepEnd("Smoke step", true, 0.85);
        client.telemetryFlush();
        report("session event", callback -> client.sessionEvent(utf8(eventData), callback));
        pollForSeconds(6.0);
        check("heartbeat", heartbeatCount > 0);
        report("session complete", callback -> client.sessionComplete(utf8(completeData), callback));
        check("session ended", client.getSessionId() == 0);
        pollForSeconds(1.0);
        check("telemetry upload", telemetryUploadCount > 0);
    }

    private static byte[] utf8(String text) {
        return text.getBytes(StandardCharsets.UTF_8);
    }

    private boolean roundTripsLaunchValue(String launchValue) {
        client.launchSet("optional", launchValue);
        return launchValue.equals(client.launchGet("optional"));
    }

    private void pollForSeconds(double durationSeconds) throws InterruptedException {
        long endNanoseconds = System.nanoTime() + (long) (durationSeconds * 1e9);
        while (System.nanoTime() < endNanoseconds) {
            client.poll();
            Thread.sleep(POLL_INTERVAL_MILLISECONDS);
        }
    }

    /** Starts one call with the given callback. */
    private interface CallStarter {
        ApexCall start(ApexClient.ResultCallback callback);
    }

    /** Starts the call, polls until its result arrives and prints a line for it. */
    private boolean report(String stepName, CallStarter callStarter) throws InterruptedException {
        final int[] callbackCount = {0};
        ApexResult result = callStarter.start(arrivedResult -> callbackCount[0]++)
            .await((long) (CALL_TIMEOUT_SECONDS * 1000));
        boolean stepPassed = result.isOk() && callbackCount[0] == 1;
        System.out.println((stepPassed ? "PASS  " : "FAIL  ") + stepName + "  " + result);
        countStep(stepPassed);
        return stepPassed;
    }

    private void check(String stepName, boolean checkPassed) {
        System.out.println((checkPassed ? "PASS  " : "FAIL  ") + stepName);
        countStep(checkPassed);
    }

    private void countStep(boolean stepPassed) {
        if (stepPassed) {
            passedStepCount++;
        } else {
            failedStepCount++;
        }
    }
}
