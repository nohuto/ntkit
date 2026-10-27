# Thread Simulation

Tool for experimenting with thread scheduling, inspired by [CPUStress](https://github.com/zodiacon/CPUStress).

## Features

- Activity per thread, or a custom work, period and waitable timer (default/high resolution)
- Relative priority, priority boost, group affinity, CPU Sets, ideal processor per thread
- Shared mutex for priority inversion
- Process priority class, affinity, CPU Sets
- Suspend, resume, stop threads
- Thread state & wait reason, base & current priority, running priority distribution, context switches, CPU % and CPU time (`QueryThreadCycleTime`)
- Some presets for specific scenarios in `assets\examples\`, see comments in each file for some additional notes:

### Quantum Expiration

See '[Priority Separation, Quantum](https://noverse.dev/docs/win-config/system/priority-separation/#quantum)' / '[Context Switching, WrQuantumEnd](https://noverse.dev/docs/windbg-notes/threads/thread-scheduling/context-switching/#wrquantumend)' for more details.

### CPU Starvation

### Priority Preemption

### Priority Boosting

### Priority Decay / Inversion

Will be added soon.

## Verify Binaries

All releases have a detached GPG sig, import the [public key](https://pgp.noverse.dev) and verify the file next to its sig:

```powershell
curl.exe -fsSL https://pgp.noverse.dev -o public.asc
gpg --import public.asc

gpg --verify thread-sim-Portable-<version>-x64.zip.sig thread-sim-Portable-<version>-x64.zip
gpg --verify thread-sim.exe.sig thread-sim.exe
```

It must show `Good signature from "nohuto <contact@noverse.dev>"`, made by the signing subkey `FF65 399D 1B28 95F6 CC34  BE79 1853 081F E103 04A2`.

## Preview

![](https://github.com/nohuto/thread-sim/blob/main/assets/images/thread-sim.png?raw=true)

## CL

```powershell
thread-sim.exe [--data-dir <dir>] [--config <file.jsonc>]
```

- `--data-dir` = `settings.json` location (default `%LOCALAPPDATA%\thread-sim`)
- `--config` opens a config at startup
