// AutomationBridgeSettings — the token read must not touch the OS secret store
// when nothing was ever saved.
//
// Every AetherSDR launch with AETHER_AUTOMATION=1 calls loadToken(). Before the
// "tokenSet" marker existed that always enqueued a QtKeychain read, and on
// macOS the first keychain call of a session raises a login-keychain prompt
// that an ad-hoc-signed build can never get "Always Allow" to stick for.
// QtKeychain runs one job at a time, and the bridge socket only listens from
// the read callback, so the prompt also held the automation bridge hostage.
//
// This test never enqueues a keychain job: the marker is seeded false before
// the first loadToken(), and saveToken() with a real token is only exercised
// where HAVE_KEYCHAIN is off (a write would land in the developer's keychain).
//
// Runs in its own process: AppSettings is a process-wide singleton.

#include "TestSettingsProfile.h"

#include "core/AppSettings.h"
#include "core/AutomationBridgeSettings.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstdlib>

using namespace AetherSDR;

static int g_failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

static const char* const kRoot = "AutomationBridge";

static QJsonObject bridgeObject()
{
    return QJsonDocument::fromJson(
               AppSettings::instance().value(kRoot, QString{}).toString().toUtf8())
        .object();
}

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("automation-bridge-settings-test"));
    QCoreApplication app(argc, argv);
    AppSettings::instance().load();
    qunsetenv("AETHER_MCP_TOKEN");

    // ---- the env override answers synchronously and writes no marker -------
    qputenv("AETHER_MCP_TOKEN", "env-token-for-test");
    {
        QString got;
        bool sync = false;
        AutomationBridgeSettings::loadToken(&app, [&](const QString& t) {
            got = t;
            sync = true;
        });
        check(sync, "env token: the callback runs before loadToken returns");
        check(got == QStringLiteral("env-token-for-test"), "env token: value is the env var");
        check(!bridgeObject().contains(QLatin1String("tokenSet")),
              "env token: no tokenSet marker is written");
    }
    qunsetenv("AETHER_MCP_TOKEN");

    // ---- marker false: no keychain job, empty answer, before the call returns
    {
        QJsonObject o = bridgeObject();
        o[QLatin1String("tokenSet")] = false;
        AppSettings::instance().setValue(
            kRoot, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));

        QString got = QStringLiteral("unset");
        bool sync = false;
        AutomationBridgeSettings::loadToken(&app, [&](const QString& t) {
            got = t;
            sync = true;
        });
        check(sync, "tokenSet=false: the callback runs before loadToken returns "
                    "(no keychain job was scheduled)");
        check(got.isEmpty(), "tokenSet=false: the answer is the empty token");
        check(bridgeObject().value(QLatin1String("tokenSet")).toBool(true) == false,
              "tokenSet=false: the marker survives the read");
    }

    // ---- the marker never lives beside the other bools' semantics ----------
    {
        AutomationBridgeSettings::setEnabled(true);
        const QJsonObject o = bridgeObject();
        check(o.value(QLatin1String("enabled")).toBool(false),
              "setEnabled keeps working with the marker present");
        check(o.contains(QLatin1String("tokenSet")) && !o.value(QLatin1String("tokenSet")).toBool(true),
              "setEnabled preserves tokenSet=false (read-modify-write of the whole object)");
        AutomationBridgeSettings::setEnabled(false);
    }

#ifndef HAVE_KEYCHAIN
    // ---- saveToken records the marker (plaintext path only: with the
    // keychain on, a save would write into the developer's real keychain) --
    {
        AutomationBridgeSettings::saveToken(QStringLiteral("abc"));
        check(bridgeObject().value(QLatin1String("tokenSet")).toBool(false),
              "saveToken(non-empty) sets tokenSet=true");
        AutomationBridgeSettings::saveToken(QString{});
        check(!bridgeObject().value(QLatin1String("tokenSet")).toBool(true),
              "saveToken(empty) sets tokenSet=false");
    }
#endif

    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("automation_bridge_settings_test: ok\n");
    return 0;
}
