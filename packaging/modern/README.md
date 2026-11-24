# Windows 11 identity prototype

The Shell DLL implements a separate `IExplorerCommand` class
`9D0F49AD-69C1-43C3-A712-F3CB64F69210`.
It provides archive extraction and ZIP/7z creation subcommands for files,
directories and multi-selection. COM tests load the DLL directly without
changing registration.

Ordinary unsigned packages **do not install or enable** this class.
`regsvr32` continues to register only the classic menu.
The prototype follows Microsoft's external-location package sample:
[AppModelSamples](https://github.com/microsoft/AppModelSamples/tree/master/Samples/PackageWithExternalLocation).

For a disposable Windows 11 acceptance machine:

1. Provide appropriately sized PNG logo assets and the exact certificate subject.
2. Run `build-identity.ps1` using the Windows SDK `makeappx.exe`, supplying the staged application directory; full manifest validation with temporary executable copies must pass.
3. Sign the MSIX, GUI and Shell DLL with a trusted certificate using `../sign.ps1`.
4. Explicitly run `deploy-identity.ps1 -EnableExperimentalModernMenu`, supplying the expected publisher and installed external location.
5. Verify real Explorer appearance, multi-selection, directory operations,
   upgrade and removal. Run `remove-identity.ps1` to remove only this publisher's identity.

No script imports certificates, trusts a development certificate, or changes
file defaults. Trusted signing keys remain outside the repository.
The `Publisher` in the manifest must match the signing certificate.

Inno installation is intentionally not wired to this experimental path yet.
Classic/new-menu duplication, deployment rollback and cross-install ownership
must pass Windows 11 acceptance before exposing a production installer task.
Manifest compilation alone is not proof that the menu appears in Explorer.
The script first validates a full package containing the declared binaries.
It then removes only those temporary copies and uses Microsoft's documented
`/nv` switch for the external-location package, whose binaries intentionally
remain outside the MSIX. No schema errors are bypassed during the first pass.
