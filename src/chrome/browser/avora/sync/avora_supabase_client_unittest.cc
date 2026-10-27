// Copyright 2026 Avora. All rights reserved.

#include "chrome/browser/avora/sync/avora_supabase_client.h"

#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace avora::sync {
namespace {

constexpr char kBase[] = "https://project.supabase.co";
constexpr char kKey[] = "sb_publishable_test";

constexpr char kSessionJson[] = R"({
  "access_token": "access-1",
  "token_type": "bearer",
  "expires_in": 3600,
  "expires_at": 1793000000,
  "refresh_token": "refresh-1",
  "user": {"id": "user-1", "email": "me@example.com"}
})";

class AvoraSupabaseClientTest : public testing::Test {
 protected:
  // A copy of the one request in flight: answering it frees the original.
  network::ResourceRequest Pending() {
    EXPECT_EQ(factory_.NumPending(), 1);
    return factory_.GetPendingRequest(0)->request;
  }

  std::string Header(const network::ResourceRequest& request,
                     std::string_view name) {
    return request.headers.GetHeader(name).value_or("<missing>");
  }

  base::DictValue UploadJson(const network::ResourceRequest& request) {
    std::optional<base::DictValue> dict = base::JSONReader::ReadDict(
        network::GetUploadData(request), base::JSON_PARSE_RFC);
    EXPECT_TRUE(dict);
    return dict ? std::move(*dict) : base::DictValue();
  }

  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory factory_;
  SupabaseClient client_{
      base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
          &factory_),
      kBase, kKey};
};

TEST_F(AvoraSupabaseClientTest, RequestEmailCode) {
  base::test::TestFuture<RequestStatus> done;
  client_.RequestEmailCode("me@example.com", done.GetCallback());
  const network::ResourceRequest request = Pending();
  EXPECT_EQ(request.url.spec(), "https://project.supabase.co/auth/v1/otp");
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(Header(request, "apikey"), kKey);
  EXPECT_FALSE(request.headers.HasHeader("Authorization"));
  EXPECT_EQ(request.credentials_mode, network::mojom::CredentialsMode::kOmit);
  base::DictValue body = UploadJson(request);
  EXPECT_EQ(*body.FindString("email"), "me@example.com");
  EXPECT_EQ(body.FindBool("create_user"), true);

  factory_.AddResponse(request.url.spec(), "{}");
  EXPECT_EQ(done.Get(), RequestStatus::kOk);
}

TEST_F(AvoraSupabaseClientTest, VerifyEmailCodeReturnsTheSession) {
  base::test::TestFuture<RequestStatus, std::optional<AuthSession>> done;
  client_.VerifyEmailCode("me@example.com", "123456", done.GetCallback());
  const network::ResourceRequest request = Pending();
  EXPECT_EQ(request.url.spec(), "https://project.supabase.co/auth/v1/verify");
  base::DictValue body = UploadJson(request);
  EXPECT_EQ(*body.FindString("type"), "email");
  EXPECT_EQ(*body.FindString("email"), "me@example.com");
  EXPECT_EQ(*body.FindString("token"), "123456");

  const base::Time before = base::Time::Now();
  factory_.AddResponse(request.url.spec(), kSessionJson);
  auto [status, session] = done.Take();
  EXPECT_EQ(status, RequestStatus::kOk);
  ASSERT_TRUE(session);
  EXPECT_EQ(session->access_token, "access-1");
  EXPECT_EQ(session->refresh_token, "refresh-1");
  EXPECT_EQ(session->user_id, "user-1");
  EXPECT_EQ(session->email, "me@example.com");
  EXPECT_GE(session->expires_at, before + base::Seconds(3600));
  EXPECT_FALSE(session->NeedsRefresh(base::Time::Now()));
  EXPECT_TRUE(session->NeedsRefresh(session->expires_at - base::Seconds(30)));
}

TEST_F(AvoraSupabaseClientTest, WrongCodeIsForbidden) {
  base::test::TestFuture<RequestStatus, std::optional<AuthSession>> done;
  client_.VerifyEmailCode("me@example.com", "000000", done.GetCallback());
  factory_.AddResponse(
      Pending().url.spec(),
      R"({"code":403,"error_code":"otp_expired","msg":"Token has expired or is invalid"})",
      net::HTTP_FORBIDDEN);
  auto [status, session] = done.Take();
  EXPECT_EQ(status, RequestStatus::kForbidden);
  EXPECT_FALSE(session);
}

TEST_F(AvoraSupabaseClientTest, RefreshSession) {
  base::test::TestFuture<RequestStatus, std::optional<AuthSession>> done;
  client_.RefreshSession("refresh-0", done.GetCallback());
  const network::ResourceRequest request = Pending();
  EXPECT_EQ(request.url.spec(),
            "https://project.supabase.co/auth/v1/token?grant_type=refresh_token");
  EXPECT_EQ(*UploadJson(request).FindString("refresh_token"), "refresh-0");
  factory_.AddResponse(request.url.spec(), kSessionJson);
  EXPECT_EQ(done.Get<0>(), RequestStatus::kOk);
  EXPECT_EQ(done.Get<1>()->refresh_token, "refresh-1");
}

TEST_F(AvoraSupabaseClientTest, RevokedRefreshTokenIsRejected) {
  base::test::TestFuture<RequestStatus, std::optional<AuthSession>> done;
  client_.RefreshSession("revoked", done.GetCallback());
  factory_.AddResponse(Pending().url.spec(),
                       R"({"error":"invalid_grant"})", net::HTTP_BAD_REQUEST);
  EXPECT_EQ(done.Get<0>(), RequestStatus::kRejected);
}

TEST_F(AvoraSupabaseClientTest, UpsertDeviceFollowsTheContract) {
  base::test::TestFuture<RequestStatus> done;
  client_.UpsertDevice("access-1", R"({"id":"d","tabs":[]})",
                       done.GetCallback());
  const network::ResourceRequest request = Pending();
  EXPECT_EQ(request.url.spec(),
            "https://project.supabase.co/rest/v1/devices?on_conflict=id");
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(Header(request, "apikey"), kKey);
  EXPECT_EQ(Header(request, "Authorization"), "Bearer access-1");
  EXPECT_EQ(Header(request, "Prefer"),
            "resolution=merge-duplicates,return=minimal");
  EXPECT_EQ(Header(request, "Content-Type"), "application/json");
  EXPECT_EQ(network::GetUploadData(request), R"({"id":"d","tabs":[]})");
  factory_.AddResponse(request.url.spec(), "", net::HTTP_CREATED);
  EXPECT_EQ(done.Get(), RequestStatus::kOk);
}

TEST_F(AvoraSupabaseClientTest, ExpiredTokenIsUnauthorized) {
  base::test::TestFuture<RequestStatus> done;
  client_.UpsertDevice("expired", "{}", done.GetCallback());
  factory_.AddResponse(Pending().url.spec(),
                       R"({"code":"PGRST303","message":"JWT expired"})",
                       net::HTTP_UNAUTHORIZED);
  EXPECT_EQ(done.Get(), RequestStatus::kUnauthorized);
}

TEST_F(AvoraSupabaseClientTest, DeleteDevice) {
  base::test::TestFuture<RequestStatus> done;
  client_.DeleteDevice("access-1", "6f1c1f5e-2b5d-4f0e-9a51-0d6c1b0f7c11",
                       done.GetCallback());
  const network::ResourceRequest request = Pending();
  EXPECT_EQ(request.url.spec(),
            "https://project.supabase.co/rest/v1/devices?id=eq."
            "6f1c1f5e-2b5d-4f0e-9a51-0d6c1b0f7c11");
  EXPECT_EQ(request.method, "DELETE");
  EXPECT_EQ(Header(request, "Authorization"), "Bearer access-1");
  factory_.AddResponse(request.url.spec(), "", net::HTTP_NO_CONTENT);
  EXPECT_EQ(done.Get(), RequestStatus::kOk);
}

TEST_F(AvoraSupabaseClientTest, ServerErrorAndRateLimit) {
  base::test::TestFuture<RequestStatus> server_error;
  client_.UpsertDevice("a", "{}", server_error.GetCallback());
  factory_.AddResponse(Pending().url.spec(), "", net::HTTP_BAD_GATEWAY);
  EXPECT_EQ(server_error.Get(), RequestStatus::kNetworkError);

  base::test::TestFuture<RequestStatus> rate_limited;
  client_.RequestEmailCode("me@example.com", rate_limited.GetCallback());
  factory_.AddResponse(Pending().url.spec(), "{}",
                       net::HTTP_TOO_MANY_REQUESTS);
  EXPECT_EQ(rate_limited.Get(), RequestStatus::kRateLimited);
}

}  // namespace
}  // namespace avora::sync
