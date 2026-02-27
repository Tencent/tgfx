import { BackupExtensionAbility, BundleVersion } from '@kit.CoreFileKit';

export default class Hello2dBackupAbility extends BackupExtensionAbility {
  async onBackup() {
  }

  async onRestore(bundleVersion: BundleVersion) {
  }
}