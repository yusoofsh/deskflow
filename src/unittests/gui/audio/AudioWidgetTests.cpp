/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioWidgetTests.h"

#include "common/Settings.h"
#include "gui/audio/AudioSharing.h"
#include "gui/audio/AudioWidget.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>

using namespace deskflow::gui::audio;

namespace {

QComboBox *roleCombo(const AudioWidget &widget)
{
  for (auto *combo : widget.findChildren<QComboBox *>())
    if (combo->findData(int(Role::Remote)) >= 0)
      return combo;
  return nullptr;
}

QLabel *statusLabel(const AudioWidget &widget, const AudioSharing &sharing)
{
  for (auto *label : widget.findChildren<QLabel *>())
    if (label->text() == sharing.status())
      return label;
  return nullptr;
}

void chooseRole(QComboBox *combo, Role role)
{
  const int index = combo->findData(int(role));
  combo->setCurrentIndex(index);
  Q_EMIT combo->activated(index);
}

} // namespace

void AudioWidgetTests::initTestCase()
{
  QDir dir;
  QVERIFY(dir.mkpath(m_settingsPath));

  QFile oldSettings(m_settingsFile);
  if (oldSettings.exists())
    oldSettings.remove();

  Settings::setSettingsFile(m_settingsFile);
  Settings::setStateFile(m_stateFile);
}

void AudioWidgetTests::showsStatusOnlyWhileSharing()
{
  AudioSharing sharing;
  AudioWidget widget(&sharing);
  auto *combo = roleCombo(widget);
  QVERIFY(combo);
  auto *status = statusLabel(widget, sharing);
  QVERIFY(status);
  QVERIFY(!status->isVisibleTo(&widget));

  chooseRole(combo, Role::Headset);
  QCOMPARE(status->text(), sharing.status());
  QVERIFY(status->isVisibleTo(&widget));

  chooseRole(combo, Role::Remote);
  QVERIFY(status->isVisibleTo(&widget));

  chooseRole(combo, Role::Off);
  QVERIFY(!status->isVisibleTo(&widget));
}

QTEST_MAIN(AudioWidgetTests)
