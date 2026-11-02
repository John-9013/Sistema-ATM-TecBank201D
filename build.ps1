$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
g++ -std=c++17 -Wall -Wextra -O2 main.cpp -o atm_tecbank.exe
g++ -std=c++17 -Wall -Wextra -O2 -mwindows -static main_gui.cpp -o atm_tecbank_gui.exe
if ($LASTEXITCODE -eq 0) { Write-Host 'Compilado: atm_tecbank.exe (consola) y atm_tecbank_gui.exe (ventana)' }
