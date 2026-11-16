// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/avora_telemetry.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace avora {

namespace {

// PostHog's project key.  It can only send events, and the website already
// ships it in its page script.
constexpr char kPostHogKey[] = "phc_e1CzzKejNL0mAqWJ774mGP5SCiiJo5xE3aOuLrLpf9k";
constexpr char kPostHogUrl[] = "https://us.i.posthog.com/i/v0/e/";

// Long enough to stay out of startup; a test URL doesn't wait.
constexpr base::TimeDelta kFirstCheckDelay = base::Minutes(1);
constexpr base::TimeDelta kTestFirstCheckDelay = base::Seconds(3);
// Catches the day changing while Avora stays open, and retries a failed send.
constexpr base::TimeDelta kCheckInterval = base::Hours(1);

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("avora_usage_statistics", R"(
        semantics {
          sender: "Avora Usage Statistics"
          description:
            "Once a day, tells Avora's developer that this copy of Avora was "
            "used, so they can count active copies and see which versions "
            "are in use."
          trigger: "The first hour Avora is open on each day."
          data:
            "A random install ID, the Avora, Chromium and macOS versions, the "
            "processor architecture, the UI language, and rough counts of "
            "windows, tabs and Spaces.  No page addresses, titles or typed "
            "text."
          destination: OTHER
          destination_other: "PostHog, Avora's analytics service."
        }
        policy {
          cookies_allowed: NO
          setting:
            "Avora Settings > Usage statistics turns this off."
        })");

std::string Today() {
  base::Time::Exploded now;
  base::Time::Now().LocalExplode(&now);
  return base::StringPrintf("%04d-%02d-%02d", now.year, now.month,
                            now.day_of_month);
}

std::string BucketTabCount(int tabs) {
  if (tabs <= 1) {
    return tabs <= 0 ? "0" : "1";
  }
  if (tabs <= 5) {
    return "2-5";
  }
  if (tabs <= 10) {
    return "6-10";
  }
  if (tabs <= 25) {
    return "11-25";
  }
  if (tabs <= 50) {
    return "26-50";
  }
  if (tabs <= 100) {
    return "51-100";
  }
  return "101+";
}

class Telemetry {
 public:
  static Telemetry& Get() {
    static base::NoDestructor<Telemetry> telemetry;
    return *telemetry;
  }

  void Start(TelemetryConfig config) {
    config_ = std::move(config);
    const base::CommandLine& command_line =
        *base::CommandLine::ForCurrentProcess();
    GURL test_url(command_line.GetSwitchValueASCII(kTelemetryUrlSwitch));
    if (test_url.is_valid()) {
      url_ = test_url;
    } else if (config_.packaged) {
      url_ = GURL(kPostHogUrl);
    } else {
      return;
    }
    first_check_.Start(
        FROM_HERE, test_url.is_valid() ? kTestFirstCheckDelay : kFirstCheckDelay,
        base::BindOnce(&Telemetry::Check, base::Unretained(this)));
    hourly_check_.Start(
        FROM_HERE, kCheckInterval,
        base::BindRepeating(&Telemetry::Check, base::Unretained(this)));
  }

  PrefService* local_state() { return config_.local_state; }

  void CancelSend() { loader_.reset(); }

 private:
  void Check() {
    PrefService* prefs = config_.local_state;
    if (!prefs || !prefs->GetBoolean(kTelemetryEnabledPref) || loader_) {
      return;
    }
    std::string today = Today();
    if (prefs->GetString(kTelemetryLastActiveDayPref) == today) {
      return;
    }

    std::string install_id = prefs->GetString(kTelemetryInstallIdPref);
    if (install_id.empty()) {
      install_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
      prefs->SetString(kTelemetryInstallIdPref, install_id);
    }

    TelemetryCounts counts;
    if (config_.counts) {
      counts = config_.counts.Run();
    }

    base::DictValue properties;
    properties.Set("app_version", config_.app_version);
    properties.Set("chromium_version", version_info::GetVersionNumber());
    properties.Set("os_version", base::SysInfo::OperatingSystemVersion());
    properties.Set("arch", base::SysInfo::OperatingSystemArchitecture());
    properties.Set("locale", config_.locale);
    properties.Set("windows", counts.windows);
    properties.Set("tabs", BucketTabCount(counts.tabs));
    properties.Set("spaces", counts.spaces);
    properties.Set("$lib", "avora-app");

    base::DictValue event;
    event.Set("api_key", kPostHogKey);
    event.Set("event", "app_active");
    event.Set("distinct_id", install_id);
    event.Set("properties", std::move(properties));

    auto request = std::make_unique<network::ResourceRequest>();
    request->url = url_;
    request->method = "POST";
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    loader_ =
        network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
    loader_->AttachStringForUpload(base::WriteJson(event).value_or("{}"),
                                   "application/json");
    loader_->DownloadHeadersOnly(
        config_.url_loader_factory.get(),
        base::BindOnce(&Telemetry::OnSent, base::Unretained(this), today));
  }

  void OnSent(std::string day,
              scoped_refptr<net::HttpResponseHeaders> headers) {
    loader_.reset();
    // Only a send PostHog took counts; anything else tries again next hour.
    if (!headers || headers->response_code() / 100 != 2) {
      return;
    }
    // Turned off while the event was on its way.
    if (!config_.local_state->GetBoolean(kTelemetryEnabledPref)) {
      return;
    }
    config_.local_state->SetString(kTelemetryLastActiveDayPref, day);
  }

  TelemetryConfig config_;
  GURL url_;
  base::OneShotTimer first_check_;
  base::RepeatingTimer hourly_check_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
};

}  // namespace

void RegisterTelemetryLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kTelemetryEnabledPref, true);
  registry->RegisterStringPref(kTelemetryInstallIdPref, std::string());
  registry->RegisterStringPref(kTelemetryLastActiveDayPref, std::string());
}

void StartTelemetry(TelemetryConfig config) {
  Telemetry::Get().Start(std::move(config));
}

bool IsTelemetryEnabled() {
  PrefService* prefs = Telemetry::Get().local_state();
  return prefs && prefs->GetBoolean(kTelemetryEnabledPref);
}

void SetTelemetryEnabled(bool enabled) {
  PrefService* prefs = Telemetry::Get().local_state();
  if (!prefs) {
    return;
  }
  prefs->SetBoolean(kTelemetryEnabledPref, enabled);
  if (!enabled) {
    Telemetry::Get().CancelSend();
    prefs->ClearPref(kTelemetryInstallIdPref);
    prefs->ClearPref(kTelemetryLastActiveDayPref);
  }
}

}  // namespace avora
