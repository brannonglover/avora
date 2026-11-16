#!/usr/bin/env node
/**
 * Syncs the marketing version from package.json into native projects when they
 * exist. During EAS builds (--with-build-number), also stamps a fresh iOS
 * CURRENT_PROJECT_VERSION and Android versionCode.
 *
 * Run manually with: node scripts/sync-version.js
 * Include fresh build numbers with: node scripts/sync-version.js --with-build-number
 * Or automatically via: npm run version:sync / npm version patch|minor|major
 */

const fs = require('fs');
const path = require('path');

const rootDir = path.resolve(__dirname, '..');
const appConfigPath = path.join(rootDir, 'app.config.js');
const includeBuildNumber = process.argv.includes('--with-build-number');

const { expo } = require(appConfigPath);
const version = expo.version;
const buildNumber = expo.ios?.buildNumber;
const versionCode = expo.android?.versionCode;

function replaceAllOrThrow(contents, pattern, replaceWith, label, fileLabel) {
  if (!pattern.test(contents)) {
    throw new Error(`Unable to find ${label} in ${fileLabel}`);
  }
  return contents.replace(pattern, replaceWith);
}

function findIosProjectFile() {
  const iosDir = path.join(rootDir, 'ios');
  if (!fs.existsSync(iosDir)) return null;
  const xcodeproj = fs.readdirSync(iosDir).find((entry) => entry.endsWith('.xcodeproj'));
  if (!xcodeproj) return null;
  return path.join(iosDir, xcodeproj, 'project.pbxproj');
}

const iosProjectPath = findIosProjectFile();
if (iosProjectPath) {
  let iosProject = fs.readFileSync(iosProjectPath, 'utf8');
  iosProject = replaceAllOrThrow(
    iosProject,
    /MARKETING_VERSION = [^;]+;/g,
    `MARKETING_VERSION = ${version};`,
    'MARKETING_VERSION',
    path.basename(iosProjectPath)
  );

  if (includeBuildNumber) {
    iosProject = replaceAllOrThrow(
      iosProject,
      /CURRENT_PROJECT_VERSION = [^;]+;/g,
      `CURRENT_PROJECT_VERSION = ${buildNumber};`,
      'CURRENT_PROJECT_VERSION',
      path.basename(iosProjectPath)
    );
  }

  fs.writeFileSync(iosProjectPath, iosProject);
} else {
  console.log('ios Xcode project not found, skipping');
}

const androidAppGradlePath = path.join(rootDir, 'android', 'app', 'build.gradle');
if (fs.existsSync(androidAppGradlePath)) {
  let androidGradle = fs.readFileSync(androidAppGradlePath, 'utf8');
  androidGradle = replaceAllOrThrow(
    androidGradle,
    /versionName "[^"]*"/,
    `versionName "${version}"`,
    'versionName',
    path.basename(androidAppGradlePath)
  );

  if (includeBuildNumber && versionCode != null) {
    androidGradle = replaceAllOrThrow(
      androidGradle,
      /versionCode \d+/,
      `versionCode ${versionCode}`,
      'versionCode',
      path.basename(androidAppGradlePath)
    );
  }

  fs.writeFileSync(androidAppGradlePath, androidGradle);
} else {
  console.log('android/app/build.gradle not found, skipping');
}

console.log(`Synced version ${version} to iOS and Android`);
if (includeBuildNumber) {
  console.log(`Stamped iOS CURRENT_PROJECT_VERSION ${buildNumber}`);
  if (versionCode != null) {
    console.log(`Stamped Android versionCode ${versionCode}`);
  }
}
