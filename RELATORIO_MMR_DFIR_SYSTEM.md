# Relatorio Tecnico - MMR DFIR System

Data de referencia: 2026-07-09

## 1. Visao geral

O MMR DFIR System e um utilitario Windows com interface ImGui/DX11 que executa um preset defensivo de verificacoes e ativacoes relacionadas a telemetria, seguranca do Windows, firmware e integridade de disco.

O programa nao e um servico Windows proprio. Ele e um aplicativo grafico elevado, com manifesto `requireAdministrator`, que aplica configuracoes no host e exibe o resultado em uma tela unica.

O objetivo operacional e preparar o ambiente para coleta/validacao defensiva, garantindo que componentes relevantes estejam ativos:

- Servicos de diagnostico e telemetria do Windows.
- Sysmon com configuracao embutida.
- TPM 2.0.
- Secure Boot.
- Windows Firewall.
- Windows Security / Microsoft Defender.
- Defender Quick Scan.
- NTFS USN Journal em discos fixos NTFS.

## 2. Arquitetura

O nucleo do sistema esta em `src/main.cpp`.

Principais blocos:

- UI e renderizacao: ImGui + DirectX 11.
- Gerenciamento de servicos: Service Control Manager via `OpenSCManagerW`, `OpenServiceW`, `QueryServiceStatusEx`, `ChangeServiceConfigW` e `StartServiceW`.
- Firewall: API COM `INetFwPolicy2`.
- TPM, Secure Boot e Defender: PowerShell oficial sem `ExecutionPolicy Bypass`.
- NTFS Journal: `fsutil usn queryjournal` e `fsutil usn createjournal`.
- Sysmon: assets embutidos em resource `RCDATA`.
- Modo de teste: ativado quando o executavel se chama `teste.exe`.

## 3. Recursos embutidos

O arquivo `mmr_service_resources.rc` embute os seguintes recursos no `.exe`:

- `src/assets/Sysmon.exe`
- `src/assets/cfg.xml`
- `src/assets/Eula.txt`
- `src/assets/mmr_icon.ico`

Tambem define metadados de versao:

- ProductName: `MMR DFIR System`
- FileDescription: `MMR DFIR System`
- CompanyName: `EkaL7 e Rarexv`
- Team: `EkaL7 e Rarexv`

Ao iniciar o preset real, o app extrai os assets do Sysmon para:

```txt
%APPDATA%\Sysmon\Sysmon.exe
%APPDATA%\Sysmon\cfg.xml
%APPDATA%\Sysmon\Eula.txt
```

Depois disso, instala ou atualiza o Sysmon:

- Se `Sysmon64` ou `Sysmon` ja existe: aplica `Sysmon.exe -accepteula -c cfg.xml`.
- Se nao existe: aplica `Sysmon.exe -accepteula -i cfg.xml`.

## 4. Preset de verificacoes

A lista atual de verificacoes vem de `RequiredServices()`:

1. `PcaSvc`
2. `DPS`
3. `DiagTrack`
4. `SysMain`
5. `SysMon`
6. `EventLog`
7. `Secure Boot`
8. `TPM 2.0`
9. `Windows Firewall`
10. `Windows Security`
11. `Defender Quick Scan`
12. `NTFS Journal`

A UI mostra isso como `12 verificacoes`, e nao como `12 servicos`, porque a lista mistura servicos, firmware, scan e recursos de disco.

## 5. Fluxo operacional real

Quando o app roda com nome normal, por exemplo `mmr service.exe`, o fluxo e:

1. Inicializa a UI.
2. Cria a lista de verificacoes.
3. Inicia uma thread de automacao.
4. Extrai os assets do Sysmon.
5. Para cada item:
   - Consulta o estado inicial.
   - Aplica a ativacao/configuracao quando possivel.
   - Consulta o estado final.
   - Define o marcador visual da linha.
6. Exibe o resumo no painel `RESULTADO`.
7. Exibe aviso de reinicializacao recomendada.

O estado da UI e protegido por mutex. A thread de automacao trabalha sobre uma copia local da lista e publica o resultado final de forma sincronizada.

## 6. Servicos Windows

Para servicos tradicionais, o app tenta seguir a ordem:

1. Consultar status.
2. Verificar/configurar inicializacao automatica.
3. Iniciar o servico se nao estiver rodando.
4. Aguardar ate 30 segundos pelo estado `SERVICE_RUNNING`.

O codigo separa permissoes para evitar falhas com servicos protegidos:

- Consulta status com acesso baixo.
- Configura automatico com handle separado.
- Solicita `SERVICE_START` apenas se precisar iniciar.

Isso evita erros como abrir `mpssvc` com permissao excessiva de alteracao quando o Firewall ja esta ativo.

## 7. TPM 2.0

O TPM e verificado por PowerShell:

- `Get-Tpm`
- `Get-CimInstance -Namespace root\CIMV2\Security\MicrosoftTpm -ClassName Win32_Tpm`

O app considera TPM ativo quando:

- `TpmPresent` e verdadeiro.
- `TpmReady` e verdadeiro.
- `SpecVersion` contem `2.0`.

Quando necessario, o app tenta inicializar TPM com:

```powershell
Initialize-Tpm -AllowClear:$false
```

Ele nao limpa TPM e nao executa operacao destrutiva de ownership.

## 8. Secure Boot

O Secure Boot e verificado por:

```powershell
Confirm-SecureBootUEFI
```

Se estiver ativo, aparece como `Ativado`.

Se estiver desativado ou indisponivel por BIOS/UEFI, a linha recebe o botao `BIOS`. Esse botao abre uma pesquisa no Google usando informacoes da placa-mae lidas do registro:

```txt
HKLM\HARDWARE\DESCRIPTION\System\BIOS
BaseBoardManufacturer
BaseBoardProduct
SystemManufacturer
SystemProductName
```

Exemplo de consulta:

```txt
B650M GAMING WIFI Gigabyte Technology Co., Ltd. como ativar secure boot
```

## 9. Windows Firewall

O Firewall e tratado em duas camadas:

1. O servico `mpssvc` e validado/iniciado quando necessario.
2. A API COM `INetFwPolicy2` ativa os perfis:
   - Domain
   - Private
   - Public

O app tambem configura:

- Firewall habilitado.
- Entrada padrao bloqueada.
- Saida padrao permitida.
- Notificacoes habilitadas.

## 10. Windows Security e Defender

O item `Windows Security` tenta garantir os servicos:

- `SecurityHealthService`
- `wscsvc`
- `WinDefend`
- `WdNisSvc`

Depois aplica protecoes do Defender com `Set-MpPreference`:

- Realtime Monitoring ligado.
- Behavior Monitoring ligado.
- Block at First Seen ligado.
- IOAV ligado.
- Script Scanning ligado.
- PUA Protection ligada.

O item `Defender Quick Scan` chama:

```powershell
Start-MpScan -ScanType QuickScan
```

O timeout de processo e de ate 30 minutos.

## 11. NTFS Journal

O `NTFS Journal` aparece na UI como uma verificacao propria.

O codigo detecta discos com:

- `GetLogicalDrives()`
- `GetDriveTypeW(root) == DRIVE_FIXED`
- `GetVolumeInformationW(...)` com filesystem `NTFS`

Para cada disco fixo NTFS com letra, o app executa:

```txt
fsutil usn queryjournal X:
```

Se o Journal nao existir:

```txt
fsutil usn createjournal m=0x800000 a=0x100000 X:
```

No host validado havia:

```txt
C: NTFS Fixed
D: NTFS Fixed
```

Portanto, o app cobre `C:` e `D:` nesse host. A cobertura e para discos fixos NTFS com letra. Volumes montados sem letra nao entram nesse fluxo.

O resumo operacional do Journal aparece no resultado e no tooltip:

```txt
NTFS Journal: X ativado(s), Y ja ativo(s), Z falha(s)
```

## 12. Estados visuais da UI

Cada linha pode receber um marcador:

- `V`: ja estava ativo antes do preset.
- `?`: foi ativado nesta execucao.
- `i`: nao foi possivel ativar automaticamente ou ficou desativado.
- `BIOS`: acao contextual para Secure Boot.

Mensagens atuais:

- Ativado recentemente:
  ```txt
  Ativado nesta execucao. Reinicie o PC para garantir inicializacao e telemetria desde o boot.
  ```

- Falha/desativado:
  ```txt
  Nao foi possivel aplicar automaticamente. Verifique politicas, otimizadores ou integridade do Windows; restauracao do sistema pode ser necessaria.
  ```

- Secure Boot:
  ```txt
  Requer ajuste na BIOS/UEFI. Clique em BIOS para pesquisar o procedimento pelo modelo da placa-mae.
  ```

## 13. Modo teste

O modo teste e ativado quando o executavel se chama:

```txt
teste.exe
```

Nesse modo, o app nao aplica o preset real no Windows. Ele simula estados visuais:

- Ativo previamente.
- Ativado agora.
- Desativado.
- Erro operacional.
- Scan concluido.
- Secure Boot pendente com botao BIOS.

Mensagem do modo teste:

```txt
MODO TESTE: estados visuais simulados; nenhuma configuracao do Windows foi alterada
```

Isso permite validar a UI sem alterar servicos, Defender, Firewall, Sysmon ou Journal.

## 14. Build, icone e assinatura

O build final fica em:

```txt
x64\Release\mmr service.exe
x64\Release\teste.exe
```

O executavel inclui:

- Icone principal `mmr_icon.ico`.
- Metadados `MMR DFIR System`.
- Assinatura Authenticode self-signed.

Certificado:

```txt
CN=MMR DFIR System, O=EkaL7 e Rarexv
```

O `.cer` publico fica em:

```txt
certs\MMR_DFIR_System.cer
```

Observacao: a assinatura e self-signed. O Windows mostra `UnknownError` enquanto o certificado nao estiver em uma cadeia confiavel. Para distribuicao publica, o correto e usar certificado de code signing emitido por CA.

## 15. Pontos fortes

- Fluxo centralizado e facil de auditar.
- Nao usa `ExecutionPolicy Bypass`.
- Usa APIs oficiais para Firewall.
- Usa comandos oficiais para Defender e TPM.
- Modo teste evita alteracoes reais no host.
- UI diferencia estados inicial, ativado agora e falha.
- Sysmon e configuracao ficam embutidos no executavel.
- Build possui icone, metadados e assinatura local.

## 16. Limitacoes conhecidas

- Requer execucao elevada.
- Servicos protegidos por politica, GPO ou Tamper Protection podem negar alteracoes.
- Defender pode ser controlado por politica corporativa.
- Secure Boot depende de BIOS/UEFI e nao pode ser ativado totalmente via Windows.
- NTFS Journal cobre discos fixos NTFS com letra; nao cobre mountpoints sem letra.
- Assinatura self-signed nao gera reputacao publica de SmartScreen.
- `teste.exe` simula estados por nome de arquivo. Renomear o binario para `teste.exe` ativa o modo teste.

## 17. Conclusao

O sistema esta funcional como ativador/verificador defensivo local. Ele cobre servicos, firmware, seguranca do Windows, Sysmon e NTFS Journal com uma UI consistente e um modo de teste seguro para validar estados visuais.

Para uso operacional real, o ponto mais importante e executar em ambiente controlado/elevado e validar logs apos reboot, especialmente para Sysmon, Defender e NTFS Journal.
