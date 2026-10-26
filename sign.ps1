param([Parameter(Mandatory)][string[]]$path)

$key = "FF65399D1B2895F6CC34BE791853081FE10304A2"
if (!(Get-Command gpg -ErrorAction SilentlyContinue) -or !(gpg --batch --list-secret-keys $key 2>$null)) { Write-Warning "signing key not found"; exit 0 }

foreach ($file in $path) {
    gpg --batch --yes --local-user "$key!" --detach-sign --output "$file.sig" $file
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    if (!(gpg --batch --status-fd 1 --verify "$file.sig" $file 2>$null | Select-String "^\[GNUPG:\] VALIDSIG $key ")) { Write-Error "sig check failed: $file"; exit 1 }
}
