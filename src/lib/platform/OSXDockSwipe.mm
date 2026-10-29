/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXDockSwipe.h"

#include "base/Log.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <dlfcn.h>
#include <mach/mach_time.h>

// The private HIDEvent class that builds IOHIDEvents
@protocol DeskflowHIDEvent <NSObject>
- (instancetype)initWithType:(uint32_t)type timestamp:(uint64_t)timestamp senderID:(uint64_t)senderID;
- (void)setOptions:(uint32_t)options;
- (void)setIntegerValue:(NSInteger)value forField:(uint32_t)field;
- (void)setDoubleValue:(double)value forField:(uint32_t)field;
- (void)appendEvent:(id)event;
@end

namespace {

using SetHIDEventFn = void (*)(CGEventRef, CFTypeRef);

// IOHIDEvent types, and their fields as type << 16 | index
constexpr uint32_t kDockSwipeType = 23;
constexpr uint32_t kVelocityType = 9;
constexpr uint32_t kDockSwipeMotion = (kDockSwipeType << 16) | 1;
constexpr uint32_t kDockSwipeProgress = (kDockSwipeType << 16) | 2;
constexpr uint32_t kDockSwipeFlavor = (kDockSwipeType << 16) | 5;
constexpr uint32_t kVelocityX = kVelocityType << 16;
constexpr uint32_t kVelocityY = (kVelocityType << 16) | 1;
constexpr NSInteger kMotionHorizontal = 1;
constexpr NSInteger kMotionVertical = 2;
constexpr NSInteger kFlavor = 3;

// phases, which go in the top byte of the event options
constexpr uint32_t kPhaseBegan = 1;
constexpr uint32_t kPhaseChanged = 2;
constexpr uint32_t kPhaseEnded = 4;
constexpr uint32_t kPhaseCancelled = 8;

// the CGEvent type that carries a gesture's HID event
constexpr auto kGestureEventType = static_cast<CGEventType>(30);

// Swipe progress for fingers moving one touchpad width. Progress 1 is a whole
// Space or a fully open Mission Control, so a third of the touchpad is a full swipe.
constexpr double kProgressPerWidth = 3.0;

// The Dock completes any ended swipe, so smaller ones are cancelled instead
constexpr double kCompleteProgress = 0.15;

// how far back the swipe's end velocity is measured
constexpr uint64_t kVelocityNanos = 100 * NSEC_PER_MSEC;

uint64_t nowNanos()
{
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

} // namespace

OSXDockSwipe::OSXDockSwipe()
{
  if (void *skyLight = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY)) {
    m_setEvent = dlsym(skyLight, "SLEventSetIOHIDEvent");
  }
  Class eventClass = NSClassFromString(@"HIDEvent");
  if (eventClass == nil && dlopen("/System/Library/PrivateFrameworks/HID.framework/HID", RTLD_LAZY) != nullptr) {
    eventClass = NSClassFromString(@"HIDEvent");
  }
  m_eventClass = (__bridge void *)eventClass;

  if (!isAvailable()) {
    LOG_WARN("touchpad swipes can't be replayed, macOS calls for them are missing");
  }
}

bool OSXDockSwipe::isAvailable() const
{
  return m_setEvent != nullptr && m_eventClass != nullptr;
}

void OSXDockSwipe::handle(const TouchpadGesture &gesture)
{
  if (!isAvailable()) {
    return;
  }

  switch (gesture.phase) {
    using enum TouchpadGesture::Phase;
  case Begin:
    begin(gesture);
    break;
  case Update:
    move(gesture);
    break;
  case End:
    move(gesture);
    end();
    break;
  case Cancel:
    cancel();
    break;
  }
}

void OSXDockSwipe::cancel()
{
  if (m_active) {
    m_active = false;
    post(kPhaseCancelled, 0.0);
  }
}

void OSXDockSwipe::begin(const TouchpadGesture &gesture)
{
  cancel();

  m_active = true;
  m_horizontal = std::abs(gesture.dx) >= std::abs(gesture.dy);
  m_progress = 0.0;
  m_samples.clear();
  move(gesture);
}

void OSXDockSwipe::move(const TouchpadGesture &gesture)
{
  if (!m_active) {
    return;
  }

  // Fingers moving right go to the Space on the left and fingers moving up open
  // Mission Control, both positive progress. Natural scrolling doesn't change it.
  const double delta = m_horizontal ? gesture.dx : -gesture.dy;
  const double progress =
      std::clamp(m_progress + kProgressPerWidth * delta / TouchpadGesture::kUnitsPerWidth, -1.0, 1.0);
  if (progress == m_progress && !m_samples.empty()) {
    return;
  }
  const bool first = m_samples.empty();
  m_progress = progress;

  const uint64_t now = nowNanos();
  m_samples.push_back({now, m_progress});
  while (m_samples.size() > 2 && now - m_samples.front().time > kVelocityNanos) {
    m_samples.pop_front();
  }

  post(first ? kPhaseBegan : kPhaseChanged, 0.0);
}

void OSXDockSwipe::end()
{
  if (!m_active) {
    return;
  }
  m_active = false;

  // a swipe that turned back before the fingers lifted is cancelled, like on a trackpad
  const double speed = velocity();
  const bool turnedBack = speed * m_progress < 0.0 && std::abs(speed) > 0.5;
  if (std::abs(m_progress) < kCompleteProgress || turnedBack) {
    LOG_DEBUG("touchpad swipe cancelled at progress %.2f", m_progress);
    post(kPhaseCancelled, speed);
  } else {
    LOG_DEBUG("touchpad swipe completed at progress %.2f", m_progress);
    post(kPhaseEnded, speed);
  }
}

double OSXDockSwipe::velocity() const
{
  // fingers that rested before lifting have no speed
  if (m_samples.size() < 2 || nowNanos() - m_samples.back().time > kVelocityNanos) {
    return 0.0;
  }
  const auto &first = m_samples.front();
  const auto &last = m_samples.back();
  const double seconds = static_cast<double>(last.time - first.time) / NSEC_PER_SEC;
  return seconds > 0.0 ? (last.progress - first.progress) / seconds : 0.0;
}

void OSXDockSwipe::post(uint32_t phase, double velocity) const
{
  @autoreleasepool {
    // the Dock ignores swipes at random without real timestamps
    const uint64_t timestamp = mach_absolute_time();
    auto eventClass = (__bridge Class)m_eventClass;

    id<DeskflowHIDEvent> swipe = [(id<DeskflowHIDEvent>)[eventClass alloc] initWithType:kDockSwipeType
                                                                              timestamp:timestamp
                                                                               senderID:0];
    [swipe setOptions:phase << 24];
    [swipe setIntegerValue:m_horizontal ? kMotionHorizontal : kMotionVertical forField:kDockSwipeMotion];
    [swipe setIntegerValue:kFlavor forField:kDockSwipeFlavor];
    [swipe setDoubleValue:m_progress forField:kDockSwipeProgress];

    if (phase == kPhaseEnded || phase == kPhaseCancelled) {
      id<DeskflowHIDEvent> speed = [(id<DeskflowHIDEvent>)[eventClass alloc] initWithType:kVelocityType
                                                                                timestamp:timestamp
                                                                                 senderID:0];
      [speed setDoubleValue:m_horizontal ? velocity : 0.0 forField:kVelocityX];
      [speed setDoubleValue:m_horizontal ? 0.0 : velocity forField:kVelocityY];
      [swipe appendEvent:speed];
      [speed release];
    }

    if (CGEventRef event = CGEventCreate(nullptr); event != nullptr) {
      CGEventSetType(event, kGestureEventType);
      reinterpret_cast<SetHIDEventFn>(m_setEvent)(event, (__bridge CFTypeRef)swipe);
      CGEventPost(kCGSessionEventTap, event);
      CFRelease(event);
    }
    [swipe release];
  }
}
