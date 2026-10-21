package com.pixovr.apex;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.ServerSocket;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/** Offline tests: the client against a local port nothing listens on. SmokeTest covers the protocol. */
class ApexClientTest {
    private static final long WAIT_MILLISECONDS = 10_000;

    private ApexClient client;

    @BeforeEach
    void createClient() throws IOException {
        String unusedUrl = unusedLocalUrl();
        client = new ApexClient(new ApexConfig(ApexEnvironment.CUSTOM, 42)
            .setHeadsetApiUrl(unusedUrl)
            .setPlatformApiUrl(unusedUrl)
            .setModuleVersion("1.0.0")
            .setRequestTimeoutMilliseconds(2000));
    }

    @AfterEach
    void closeClient() {
        client.close();
    }

    private static String unusedLocalUrl() throws IOException {
        try (ServerSocket probeSocket = new ServerSocket(0)) {
            return "http://127.0.0.1:" + probeSocket.getLocalPort();
        }
    }

    @Test
    void loadsTheBundledLibrary() {
        assertTrue(ApexNativeLibrary.getLoadedFrom() != null);
        assertEquals("0.2.0", ApexClient.getCoreVersion());
        assertEquals(ApexNativeBridge.EXPECTED_ABI_MINOR, ApexClient.getCoreAbiVersion() & 0xFFFF);
    }

    @Test
    void checksModuleVersions() {
        assertTrue(ApexClient.isModuleVersionValid("1.4.0"));
        assertFalse(ApexClient.isModuleVersionValid("0.4.0"));
        assertFalse(ApexClient.isModuleVersionValid("1.4"));
    }

    @Test
    void customEnvironmentNeedsBothUrls() {
        assertThrows(IllegalArgumentException.class,
            () -> new ApexClient(new ApexConfig(ApexEnvironment.CUSTOM, 1)));
    }

    @Test
    void resultsArriveOnlyOnPoll() {
        ApexCall call = client.getUserModules(-1, null);
        assertFalse(call.isDone());
        client.poll();
        assertTrue(call.isDone());
        assertEquals(ApexErrorCode.NOT_LOGGED_IN, call.getResult().getError());
        assertNull(call.getResult().getJson());
        ApexException failure = assertThrows(ApexException.class, () -> call.getResult().throwIfFailed());
        assertSame(call.getResult(), failure.getResult());
    }

    @Test
    void callbacksReceiveTheResult() throws InterruptedException {
        List<ApexResult> receivedResults = new ArrayList<>();
        ApexCall call = client.checkModuleAccess(-1, receivedResults::add);
        ApexResult result = client.waitFor(call, WAIT_MILLISECONDS);
        assertSame(call.getResult(), result);
        assertEquals(1, receivedResults.size());
        assertSame(result, receivedResults.get(0));
    }

    @Test
    void reportsNetworkFailures() throws InterruptedException {
        ApexResult result = client.ping(null).await(WAIT_MILLISECONDS);
        assertEquals(ApexErrorCode.NETWORK, result.getError());
        assertEquals(0, result.getHttpStatus());
        assertFalse(result.getMessage().isEmpty());
    }

    @Test
    void cancelsCalls() throws InterruptedException {
        ApexCall call = client.login("ada", "secret", null);
        call.cancel();
        assertEquals(ApexErrorCode.CANCELLED, call.await(WAIT_MILLISECONDS).getError());
    }

    @Test
    void startsLoggedOut() {
        assertFalse(client.isLoggedIn());
        assertEquals("", client.getCurrentUser());
        assertEquals(0, client.getSessionId());
    }

    @Test
    void keepsLaunchArgumentsInUtf8() {
        client.launchParseArgs(new String[] {"module.exe", "-pixotoken", "token-1"});
        assertEquals("token-1", client.launchGet("pixotoken"));
        String launchValue = "ü-✓-😀";
        client.launchSet("optional", launchValue);
        assertEquals(launchValue, client.launchGet("optional"));
        assertEquals("", client.launchGet("missing"));
        assertTrue(client.launchExitTarget(null).contains("\"action\""));
    }

    @Test
    void closesFromInsideACallback() {
        ApexCall closingCall = client.getUserModules(-1, result -> client.close());
        ApexCall laterCall = client.checkModuleAccess(-1, null);
        client.poll();
        assertTrue(closingCall.isDone());
        assertFalse(laterCall.isDone());
        assertTrue(client.isClosed());
        assertThrows(IllegalStateException.class, () -> client.ping(null));
    }

    @Test
    void refusesOtherThreads() throws InterruptedException {
        AtomicReference<Throwable> threadError = new AtomicReference<>();
        Thread otherThread = new Thread(() -> {
            try {
                client.poll();
            } catch (Throwable pollError) {
                threadError.set(pollError);
            }
        });
        otherThread.start();
        otherThread.join();
        assertTrue(threadError.get() instanceof IllegalStateException);
    }

    @Test
    void namesThisDesktop() {
        assertTrue(ApexNativeLibrary.getDesktopPlatform().matches("(linux|windows)-(x86_64|aarch64)|macos"));
    }
}
