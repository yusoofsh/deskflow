/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#import "OSXHelpers.h"

#import <Cocoa/Cocoa.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreData/CoreData.h>
#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>
#import <UserNotifications/UNNotification.h>
#import <UserNotifications/UNNotificationContent.h>
#import <UserNotifications/UNNotificationTrigger.h>
#import <UserNotifications/UNUserNotificationCenter.h>
#import <objc/runtime.h>

#import <QtGlobal>

#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace {
std::function<bool()> s_shouldQuit;
IMP s_originalShouldTerminate = nullptr;
BOOL s_isSystemShuttingDown = NO;
std::function<void()> s_onReopen;
IMP s_originalShouldHandleReopen = nullptr;
} // namespace

void requestOSXNotificationPermission()
{
#if OSX_DEPLOYMENT_TARGET >= 1014
  if (isOSXDevelopmentBuild()) {
    qWarning("Not requesting notification permission in dev build");
    return;
  }

  UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
  [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert + UNAuthorizationOptionSound)
                        completionHandler:^(BOOL granted, NSError *_Nullable error) {
                          if (error != nil) {
                            qWarning(
                                "Notification permission request error: %s",
                                [[NSString stringWithFormat:@"%@", error] UTF8String]
                            );
                          }
                        }];
#endif
}

bool isOSXDevelopmentBuild()
{
  std::string bundleURL = [[[NSBundle mainBundle] bundleURL].absoluteString UTF8String];
  return (bundleURL.find("Applications/Deskflow.app") == std::string::npos);
}

bool showOSXNotification(const QString &title, const QString &body)
{
#if OSX_DEPLOYMENT_TARGET >= 1014
  // accessing notification center on unsigned build causes an immidiate
  // application shutodown (in this case, server) and cannot be caught
  // to avoid issues with it need to first check if this is a dev build
  if (isOSXDevelopmentBuild()) {
    qWarning("Not showing notification in dev build");
    return false;
  }

  requestOSXNotificationPermission();

  UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];

  UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
  content.title = title.toNSString();
  content.body = body.toNSString();

  // Create the request object.
  UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:@"SecureInput"
                                                                        content:content
                                                                        trigger:nil];

  [center
      addNotificationRequest:request
       withCompletionHandler:^(NSError *_Nullable error) {
         if (error != nil) {
           qWarning("Notification display request error: %s", [[NSString stringWithFormat:@"%@", error] UTF8String]);
         }
       }];
#else
  NSUserNotification *notification = [[NSUserNotification alloc] init];
  notification.title = title.toNSString();
  notification.informativeText = body.toNSString();
  notification.soundName = NSUserNotificationDefaultSoundName; // Will play a default sound
  [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
  [notification autorelease];
#endif
  return true;
}

bool isOSXInterfaceStyleDark()
{
  // Implementation from http://stackoverflow.com/a/26472651
  NSDictionary *dict = [[NSUserDefaults standardUserDefaults] persistentDomainForName:NSGlobalDomain];
  id style = [dict objectForKey:@"AppleInterfaceStyle"];
  return (style && [style isKindOfClass:[NSString class]] && NSOrderedSame == [style caseInsensitiveCompare:@"dark"]);
}

void forceAppActive()
{
  [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
  [[NSApplication sharedApplication] setActivationPolicy:NSApplicationActivationPolicyRegular];
}

void macOSNativeHide()
{
  [NSApp hide:nil];
  [[NSApplication sharedApplication] setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

void setLaunchAtLogin(bool enabled)
{
  if (@available(macOS 13.0, *)) {
    SMAppService *service = [SMAppService mainAppService];
    // an app that never registered can report not found rather than not registered
    const bool isOn = service.status == SMAppServiceStatusEnabled;
    const bool isTurnedOffByUser = service.status == SMAppServiceStatusRequiresApproval;
    NSError *error = nil;
    if (enabled) {
      // leave a login item the user turned off in System Settings off
      if (!isOn && !isTurnedOffByUser && ![service registerAndReturnError:&error]) {
        qWarning("failed to add login item: %s", error.localizedDescription.UTF8String);
      }
    } else if ((isOn || isTurnedOffByUser) && ![service unregisterAndReturnError:&error]) {
      qWarning("failed to remove login item: %s", error.localizedDescription.UTF8String);
    }
    qInfo(
        "login item status: %ld (0 not registered, 1 on, 2 off in System Settings, 3 not found)",
        static_cast<long>(service.status)
    );
  }
}

static NSApplicationTerminateReply deskflow_applicationShouldTerminate(id self, SEL _cmd, NSApplication *sender)
{
  // Don't intercept a system shutdown (or logoff/restart)
  if (!s_isSystemShuttingDown && s_shouldQuit && !s_shouldQuit()) {
    return NSTerminateCancel;
  }

  // Execute Qt's applicationShouldTerminate
  if (s_originalShouldTerminate) {
    using ShouldTerminateFn = NSApplicationTerminateReply (*)(id, SEL, NSApplication *);
    return reinterpret_cast<ShouldTerminateFn>(s_originalShouldTerminate)(self, _cmd, sender);
  }

  return NSTerminateNow;
}

void installQuitHandler(std::function<bool()> shouldQuit)
{
  s_shouldQuit = std::move(shouldQuit);

  Class cls = [[NSApp delegate] class];
  SEL selector = @selector(applicationShouldTerminate:);

  Method method = class_getInstanceMethod(cls, selector);
  if (method) {
    s_originalShouldTerminate = method_getImplementation(method);
    method_setImplementation(method, (IMP)deskflow_applicationShouldTerminate);
  } else {
    class_addMethod(cls, selector, (IMP)deskflow_applicationShouldTerminate, "l@:@");
  }

  // shutdown is also triggered for logout/restart
  [[[NSWorkspace sharedWorkspace] notificationCenter] addObserverForName:NSWorkspaceWillPowerOffNotification
                                                                  object:nil
                                                                   queue:[NSOperationQueue mainQueue]
                                                              usingBlock:^(NSNotification *note) {
                                                                Q_UNUSED(note)
                                                                s_isSystemShuttingDown = YES;
                                                              }];
}

namespace {
QString macAudioDeviceName(AudioObjectID device)
{
  AudioObjectPropertyAddress address{
      kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
  };
  CFStringRef name = nullptr;
  UInt32 size = sizeof(name);
  if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &name) != noErr || !name)
    return {};
  const auto result = QString::fromCFString(name);
  CFRelease(name);
  return result;
}

bool macAudioDeviceHasStreams(AudioObjectID device, bool input)
{
  AudioObjectPropertyAddress address{
      kAudioDevicePropertyStreams, input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput,
      kAudioObjectPropertyElementMain
  };
  UInt32 size = 0;
  return AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) == noErr && size > 0;
}

AudioObjectPropertyAddress macDefaultDeviceAddress(bool input)
{
  return {
      input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
      kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
  };
}

AudioObjectID macAudioDeviceForUid(const QString &uid)
{
  AudioObjectPropertyAddress address{
      kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
  };
  CFStringRef uidString = uid.toCFString();
  AudioObjectID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  const auto status =
      AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(uidString), &uidString, &size, &device);
  CFRelease(uidString);
  return status == noErr ? device : kAudioObjectUnknown;
}
} // namespace

QString macDefaultAudioDevice(bool input)
{
  auto address = macDefaultDeviceAddress(input);
  AudioObjectID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) != noErr)
    return {};
  return macAudioDeviceName(device);
}

bool macSetDefaultAudioDevice(bool input, const QString &name)
{
  AudioObjectPropertyAddress address{
      kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
  };
  UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr)
    return false;
  std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr)
    return false;

  for (const auto device : devices) {
    if (macAudioDeviceName(device) != name || !macAudioDeviceHasStreams(device, input))
      continue;
    auto target = macDefaultDeviceAddress(input);
    return AudioObjectSetPropertyData(kAudioObjectSystemObject, &target, 0, nullptr, sizeof(device), &device) == noErr;
  }
  return false;
}

bool macAudioDeviceExists(const QString &uid)
{
  return macAudioDeviceForUid(uid) != kAudioObjectUnknown;
}

bool macAudioDeviceHidden(const QString &uid)
{
  const auto device = macAudioDeviceForUid(uid);
  AudioObjectPropertyAddress address{
      kAudioDevicePropertyIsHidden, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
  };
  UInt32 hidden = 0;
  UInt32 size = sizeof(hidden);
  return device != kAudioObjectUnknown &&
         AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &hidden) == noErr && hidden != 0;
}

bool macCreatePrivateAudioDevice(const QString &uid, const QString &name, const QString &hiddenUid)
{
  NSDictionary *description = @{
    @kAudioAggregateDeviceUIDKey : uid.toNSString(),
    @kAudioAggregateDeviceNameKey : name.toNSString(),
    @kAudioAggregateDeviceIsPrivateKey : @YES,
    @kAudioAggregateDeviceMainSubDeviceKey : hiddenUid.toNSString(),
    @kAudioAggregateDeviceSubDeviceListKey : @[
      @{@kAudioSubDeviceUIDKey : hiddenUid.toNSString()}
    ],
  };
  AudioObjectID device = kAudioObjectUnknown;
  return AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)description, &device) == noErr;
}

void whenLaunchFinished(std::function<void(bool launchedAsLoginItem)> callback)
{
  // A modal dialog shown before this runs the event loop and finishes launching early, so the launch event is gone
  if (NSRunningApplication.currentApplication.finishedLaunching) {
    callback(false);
    return;
  }

  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  // queue:nil runs the block inside the post, while the launch Apple event is still the current event
  __block id observer =
      [center addObserverForName:NSApplicationDidFinishLaunchingNotification
                          object:NSApp
                           queue:nil
                      usingBlock:^(NSNotification *note) {
                        Q_UNUSED(note)
                        NSAppleEventDescriptor *event = [NSAppleEventManager sharedAppleEventManager].currentAppleEvent;
                        callback(
                            event.eventID == kAEOpenApplication &&
                            [event paramDescriptorForKeyword:keyAEPropData].enumCodeValue == keyAELaunchedAsLogInItem
                        );
                        // last, as removing the observer can free this block and the callback it holds
                        [center removeObserver:observer];
                      }];
}

static BOOL deskflow_applicationShouldHandleReopen(id self, SEL _cmd, NSApplication *sender, BOOL hasVisibleWindows)
{
  // Qt's handler only reports the activation and never shows a window
  BOOL shouldHandle = YES;
  if (s_originalShouldHandleReopen) {
    using ShouldHandleReopenFn = BOOL (*)(id, SEL, NSApplication *, BOOL);
    shouldHandle =
        reinterpret_cast<ShouldHandleReopenFn>(s_originalShouldHandleReopen)(self, _cmd, sender, hasVisibleWindows);
  }

  if (s_onReopen)
    s_onReopen();

  return shouldHandle;
}

void installReopenHandler(std::function<void()> onReopen)
{
  s_onReopen = std::move(onReopen);

  Class cls = [[NSApp delegate] class];
  SEL selector = @selector(applicationShouldHandleReopen:hasVisibleWindows:);

  Method method = class_getInstanceMethod(cls, selector);
  if (method) {
    s_originalShouldHandleReopen = method_getImplementation(method);
    method_setImplementation(method, (IMP)deskflow_applicationShouldHandleReopen);
  } else {
    const auto types = protocol_getMethodDescription(@protocol(NSApplicationDelegate), selector, NO, YES).types;
    class_addMethod(cls, selector, (IMP)deskflow_applicationShouldHandleReopen, types);
  }
}
