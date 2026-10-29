# Thread Simulation

Tool for experimenting with thread scheduling, inspired by [CPUStress](https://github.com/zodiacon/CPUStress).

## Features

- Activity per thread, or a custom work, period and waitable timer (default/high resolution)
- Wake source per thread, waitable timer (priority increment 0), an event (1) or a window message (2)
- Start delay per thread, for example so one thread owns the shared mutex first
- Relative priority (idle to time critical), priority boost, group affinity, CPU sets, ideal processor per thread
- Shared mutex for priority inversion
- Process priority class, affinity, CPU sets
- Change priority, priority boost, affinity, CPU sets, ideal processor, process settings while running 
- Suspend, resume, stop threads
- Thread state & wait reason, base & current priority, running priority distribution, context switches, CPU % and CPU time (`QueryThreadCycleTime`)
- Some presets for specific scenarios in `assets\examples\`, see comments in each file for some additional notes:

### CPU Starvation

![](https://github.com/nohuto/thread-sim/blob/main/assets/images/cpu-starvation/cpu-starvation.png?raw=true)
![](https://github.com/nohuto/thread-sim/blob/main/assets/images/cpu-starvation/cpu-starvation-cs.png?raw=true)

### Priority Inversion

![](https://github.com/nohuto/thread-sim/blob/main/assets/images/priority-inversion/prio-inversion.png?raw=true)

### Priority Preemption

![](https://github.com/nohuto/thread-sim/blob/main/assets/images/priority-preemption/prio-preemption.png?raw=true)
![](https://github.com/nohuto/thread-sim/blob/main/assets/images/priority-preemption/prio-preemption-cs.png?raw=true)
![](https://github.com/nohuto/thread-sim/blob/main/assets/images/priority-preemption/prio-preemption-core.png?raw=true)

### Priority Boosting

![](https://github.com/nohuto/thread-sim/blob/main/assets/images/priority-boost/prio-boost.png?raw=true)

### Priority Decay

![](https://github.com/nohuto/thread-sim/blob/main/assets/images/priority-decay/prio-decay.png?raw=true)
![](https://github.com/nohuto/thread-sim/blob/main/assets/images/priority-decay/prio-decay-time.png?raw=true)

### Quantum Expiration

See '[Priority Separation, Duration Captures](https://noverse.dev/docs/win-config/system/priority-separation/#duration-captures)' / '[Context Switching, WrQuantumEnd](https://noverse.dev/docs/windbg-notes/threads/thread-scheduling/context-switching/#wrquantumend)' for details/notes on the captures.

![](https://github.com/nohuto/thread-sim/blob/main/assets/images/quantum-expiration/quantum-expiration-cs.png?raw=true)
![](https://github.com/nohuto/thread-sim/blob/main/assets/images/quantum-expiration/quantum-expiration-core.png?raw=true)
![](https://github.com/nohuto/thread-sim/blob/main/assets/images/quantum-expiration/quantum-expiration-time.png?raw=true)

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

## CLI

```powershell
thread-sim.exe [--data-dir <dir>] [--config <file.jsonc>]
```

- `--data-dir` = `settings.json` location (default `%LOCALAPPDATA%\thread-sim`)
- `--config` opens a config at startup
