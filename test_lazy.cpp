/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <HidlService.h>

#include <android/hidl/manager/1.2/IClientCallback.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hidl::base::V1_0::IBase;
using ::android::hidl::manager::implementation::HidlService;
using ::android::hidl::manager::V1_2::IClientCallback;
using ::android::sp;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::NiceMock;

class RecordingClientCallback : public IClientCallback {
public:
    Return<void> onClients(const sp<IBase>& /*base*/, bool clients) override {
        stream.push_back(clients);
        return Void();
    }

    std::vector<bool> stream;
};

class MockHidlService : public HidlService {
public:
    MockHidlService() : HidlService("fqname", "instance") {}
    MOCK_METHOD0(getNodeStrongRefCount, ssize_t());
};

class HidlServiceLazyTest : public ::testing::Test {
public:
    // Note that this should include one count for hwservicemanager. A count of
    // 1 indicates that hwservicemanager is the only process holding the service.
    void setReportedClientCount(ssize_t count) {
        mInjectedReportCount = count;
    }

    std::unique_ptr<HidlService> makeService() {
        auto service = std::make_unique<NiceMock<MockHidlService>>();
        ON_CALL(*service, getNodeStrongRefCount()).WillByDefault(Invoke([&]() { return mInjectedReportCount; }));
        return service;
    }

protected:
    void SetUp() override {
        mInjectedReportCount = -1;
    }

    ssize_t mInjectedReportCount = -1;
};

TEST_F(HidlServiceLazyTest, NoChange) {
    sp<RecordingClientCallback> cb = new RecordingClientCallback;

    std::unique_ptr<HidlService> service = makeService();
    service->addClientCallback(cb);

    setReportedClientCount(1);

    for (size_t i = 0; i < 100; i++) {
        service->handleClientCallbacks(true /*onInterval*/);
    }

    ASSERT_THAT(cb->stream, ElementsAre());
}

TEST_F(HidlServiceLazyTest, GetAndDrop) {
    sp<RecordingClientCallback> cb = new RecordingClientCallback;

    std::unique_ptr<HidlService> service = makeService();
    service->addClientCallback(cb);

    // some other process has the service
    setReportedClientCount(2);
    service->handleClientCallbacks(true /*onInterval*/);

    ASSERT_THAT(cb->stream, ElementsAre(true));

    // just hwservicemanager has the service
    setReportedClientCount(1);
    service->handleClientCallbacks(true /*onInterval*/);

    ASSERT_THAT(cb->stream, ElementsAre(true));
    service->handleClientCallbacks(true /*onInterval*/);

    ASSERT_THAT(cb->stream, ElementsAre(true, false)); // reported only after two intervals
}

TEST_F(HidlServiceLazyTest, GetGuarantee) {
    sp<RecordingClientCallback> cb = new RecordingClientCallback;

    std::unique_ptr<HidlService> service = makeService();
    service->addClientCallback(cb);

    service->guaranteeClient();

    setReportedClientCount(1);
    service->handleClientCallbacks(false /*onInterval*/);
    ASSERT_THAT(cb->stream, ElementsAre(true));

    service->handleClientCallbacks(true /*onInterval*/);
    ASSERT_THAT(cb->stream, ElementsAre(true));

    service->handleClientCallbacks(true /*onInterval*/);
    ASSERT_THAT(cb->stream, ElementsAre(true, false)); // reported only after two intervals
}

TEST_F(HidlServiceLazyTest, ManyUpdatesOffInterval) {
    sp<RecordingClientCallback> cb = new RecordingClientCallback;

    std::unique_ptr<HidlService> service = makeService();
    service->addClientCallback(cb);

    // Clients can appear and dissappear as many times as necessary, but they are only considered
    // dropped when the fixed interval stops.
    for (size_t i = 0; i < 100; i++) {
        setReportedClientCount(2);
        service->handleClientCallbacks(false /*onInterval*/);
        setReportedClientCount(1);
        service->handleClientCallbacks(false /*onInterval*/);
    }

    ASSERT_THAT(cb->stream, ElementsAre(true));

    service->handleClientCallbacks(true /*onInterval*/);
    ASSERT_THAT(cb->stream, ElementsAre(true));

    service->handleClientCallbacks(true /*onInterval*/);
    ASSERT_THAT(cb->stream, ElementsAre(true, false)); // reported only after two intervals
}

TEST_F(HidlServiceLazyTest, AcquisitionAfterGuarantee) {
    sp<RecordingClientCallback> cb = new RecordingClientCallback;

    std::unique_ptr<HidlService> service = makeService();
    service->addClientCallback(cb);

    setReportedClientCount(2);
    service->handleClientCallbacks(false /*onInterval*/);
    ASSERT_THAT(cb->stream, ElementsAre(true));

    setReportedClientCount(1);
    service->guaranteeClient();

    service->handleClientCallbacks(false /*onInterval*/);
    ASSERT_THAT(cb->stream, ElementsAre(true));

    service->handleClientCallbacks(true /*onInterval*/);
    ASSERT_THAT(cb->stream, ElementsAre(true));

    service->handleClientCallbacks(true /*onInterval*/);
    ASSERT_THAT(cb->stream, ElementsAre(true, false)); // reported only after two intervals
}
