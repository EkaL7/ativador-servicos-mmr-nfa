# MMR DFIR System

Projeto de estudo sobre **desenvolvimento de servicos Windows em C++** com analise de **DFIR (Digital Forensics & Incident Response)**.

## Objetivo

Este projeto foi criado para fins educacionais, abordando os seguintes topicos:

- Interacao com o **Service Control Manager (SCM)** do Windows via API nativa (`OpenSCManagerW`, `OpenServiceW`, `StartServiceW`).
- Consulta e ativacao de servicos de telemetria e diagnostico (`PcaSvc`, `DPS`, `DiagTrack`, `SysMain`, `EventLog`).
- Verificacao de componentes de seguranca: **TPM 2.0**, **Secure Boot**, **Windows Firewall**, **Windows Defender**.
- Instalacao e configuracao do **Sysmon** com regras DFIR (configuracao SwiftOnSecurity).
- Gerenciamento do **NTFS USN Journal** em discos fixos.
- Execucao de **Defender Quick Scan** via PowerShell.
- Interface grafica com **ImGui + DirectX 11**.
- Automacao de preset defensivo com feedback visual em tempo real.

## Estrutura do Projeto

```
mmr-service/
├── src/
│   ├── main.cpp                  # Codigo principal (UI + logica de servicos)
│   ├── resource.h                # Definicoes de recursos
│   └── assets/
│       ├── cfg.xml               # Configuracao Sysmon (DFIR)
│       ├── Eula.txt              # EULA do Sysmon
│       └── mmr_icon.ico          # Icone do aplicativo
├── external/
│   └── imgui/                    # Biblioteca ImGui (MIT License)
├── ServiceActivator/             # Ferramenta auxiliar em C# (.NET 9 WinForms)
│   ├── Program.cs
│   ├── Form1.cs
│   ├── Form1.Designer.cs
│   └── ServiceActivator.csproj
├── mmr_service_resources.rc      # Script de recursos (icone, assets embutidos)
├── mmr service.vcxproj           # Projeto Visual Studio (C++)
├── mmr service.vcxproj.filters   # Filtros do projeto
├── mmr service.sln               # Solucao Visual Studio
└── RELATORIO_MMR_DFIR_SYSTEM.md  # Relatorio tecnico detalhado
```

## Tecnologias Estudadas

| Tecnologia | Uso no Projeto |
|---|---|
| C++ 17 | Logica principal, API Windows |
| Win32 API | SCM, Registry, Firewall COM, processos |
| DirectX 11 | Renderizacao da interface |
| ImGui | Framework de UI imediata |
| PowerShell | TPM, Secure Boot, Defender |
| Sysmon | Monitoramento de eventos (DFIR) |
| C# / .NET 9 | Ferramenta auxiliar WinForms |

## Componentes Verificados pelo Preset

1. **PcaSvc** - Program Compatibility Assistant
2. **DPS** - Diagnostic Policy Service
3. **DiagTrack** - Connected User Experiences and Telemetry
4. **SysMain** - Superfetch / SysMain
5. **Sysmon** - System Monitor (instalacao automatica)
6. **EventLog** - Windows Event Log
7. **Secure Boot** - Verificacao de firmware
8. **TPM 2.0** - Trusted Platform Module
9. **Windows Firewall** - Perfis Domain, Private e Public
10. **Windows Security** - Defender, Real-Time Protection
11. **Defender Quick Scan** - Varredura rapida
12. **NTFS Journal** - USN Journal em discos fixos

## Como Compilar

### Requisitos

- Visual Studio 2022 (v143 toolset)
- Windows SDK 10.0
- .NET 9 SDK (para o ServiceActivator)

### Build

1. Abra `mmr service.sln` no Visual Studio.
2. Selecione a configuracao `Release | x64`.
3. Compile o projeto (Ctrl+Shift+B).

> **Nota:** O binario `src/assets/Sysmon.exe` nao esta incluso no repositorio. Para compilar com o recurso embutido do Sysmon, baixe o Sysmon oficial da Microsoft Sysinternals e coloque em `src/assets/Sysmon.exe`.

## Modo de Teste

Renomeie o executavel compilado para `teste.exe` para ativar o modo de teste visual. Nesse modo, nenhuma configuracao real do Windows e alterada - apenas estados visuais simulados sao exibidos na interface.

## Aviso Legal

Este projeto e exclusivamente para **estudo e aprendizado**. Nao utilize em ambientes de producao sem as devidas adaptacoes e autorizacoes. A execucao requer privilegios de administrador e altera configuracoes de servicos do Windows.

## Equipe

Desenvolvido por **EkaL7** e **Rarexv**.

## Licenca

Projeto educacional. A biblioteca ImGui utilizada e distribuida sob [licenca MIT](https://github.com/ocornut/imgui/blob/master/LICENSE.txt).
