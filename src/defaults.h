#ifndef KEYSWITCHFIX_DEFAULTS_H
#define KEYSWITCHFIX_DEFAULTS_H

/* Programs where nothing is ever corrected, shared by the app and Setup:
   password managers and the Windows credential and lock screens. */
#define KS_DEFAULT_EXCLUDED \
    L"1Password.exe,Bitwarden.exe,CredentialUIBroker.exe,Dashlane.exe,Enpass.exe," \
    L"KeePass.exe,KeePassXC.exe,KeeperPasswordManager.exe,LastPass.exe,LockApp.exe," \
    L"NordPass.exe,Proton Pass.exe,RoboForm.exe"

#endif
