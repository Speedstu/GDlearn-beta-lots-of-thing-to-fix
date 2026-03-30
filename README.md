# GDLearnCPP — Geometry Dash Machine Learning Bot

A C++ reinforcement learning framework for training a bot to beat **any** Geometry Dash level, inspired by [GigaLearnCPP](https://github.com/ZealanL/GigaLearnCPP-Leak) for Rocket League.

## How It Works

### Architecture (like GigaLearn but for GD)
```
┌─────────────────────────────────────────────────┐
│  GDLearnCPP                                     │
│                                                  │
│  ┌──────────┐   ┌─────────────┐   ┌──────────┐ │
│  │  Level    │──▶│  Simulator  │──▶│  Env     │ │
│  │  Parser   │   │  (Physics)  │   │  (Obs +  │ │
│  └──────────┘   └─────────────┘   │  Rewards) │ │
│                        │           └─────┬────┘ │
│                   1000x speed            │      │
│                                    ┌─────▼────┐ │
│                                    │  PPO +   │ │
│                                    │  Genetic │ │
│                                    │  Agent   │ │
│                                    └─────┬────┘ │
│                                          │      │
│  ┌──────────┐   ┌─────────────┐   ┌─────▼────┐ │
│  │  Memory   │◀──│  Input      │◀──│  Trained │ │
│  │  Reader   │   │  Injector   │   │  Model   │ │
│  └──────────┘   └─────────────┘   └──────────┘ │
│       ▲               │                         │
│       │               ▼                         │
│  ┌────┴───────────────────┐                     │
│  │   Real Geometry Dash   │                     │
│  └────────────────────────┘                     │
└─────────────────────────────────────────────────┘
```

### Training Pipeline
1. **Level Parser** — Reads real GD level files (base64+gzip encoded) or generates test levels
2. **Simulator** — Runs GD physics internally at **1000x+ real-time speed** (no rendering)
3. **Environment** — Builds observations (player state + 20x14 grid scan ahead) and computes rewards
4. **Agent** — Learns via:
   - **Genetic Algorithm** — Fast exploration with 1000 population, mutation + crossover
   - **PPO** — Precise gradient-based optimization with Adam optimizer
   - **Hybrid** — Genetic warmup → PPO fine-tuning (recommended)
5. **Live Play** — Reads game memory + sends inputs to play on real GD

### Observation Space (what the bot sees)
- **Player state** (14 features): position, velocity, speed, ground status, gravity, gamemode (one-hot)
- **Grid scan** (20×14×2 = 560 features): solid blocks and hazards in a grid ahead of the player
- **Total**: 574 floats per frame

### Reward System (GigaLearn-style, modular + weighted)
| Reward | Weight | Description |
|--------|--------|-------------|
| Progress | 5.0 | Forward movement |
| Death | 3.0 | Penalty on dying |
| Survival | 0.5 | Small bonus per step alive |
| Completion | 10.0 | Huge bonus for finishing level |
| Speed | 0.5 | Maintaining speed |
| SmoothFlight | 0.2 | Smooth Y in ship/UFO/wave modes |
| Altitude | 0.1 | Penalty for extreme Y positions |
| Milestone | 2.0 | Bonus at every 10% progress |

### Supported Gamemodes
Cube, Ship, Ball, UFO, Wave, Robot, Spider, Swing Copter

## Build

### Requirements
- **CMake** 3.18+
- **MSVC** (Visual Studio 2019/2022) or MinGW with C++17
- **Windows** (for memory reading + input injection)
- **LibTorch** (optional — built-in neural net works without it)

### Build Steps
```powershell
cd G:\gd-ml-bot
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

Or with LibTorch:
```powershell
cmake .. -G "Visual Studio 17 2022" -DTORCH_DIR=G:/libtorch
cmake --build . --config Release
```

## Usage

### Quick Test (no GD needed)
```powershell
.\Release\GDLearnCPP.exe test
```
Runs simulator benchmark and verifies everything works.

### Train on Generated Levels
```powershell
.\Release\GDLearnCPP.exe train --mode hybrid --levels test --steps 10000000
```

### Train on Real GD Levels
```powershell
.\Release\GDLearnCPP.exe train --mode hybrid --levels all --steps 50000000
```

### Train on a Specific Level
```powershell
.\Release\GDLearnCPP.exe train --levels 1 --steps 5000000
```

### Play on Real GD (live mode)
```powershell
# Start Geometry Dash first, then:
.\Release\GDLearnCPP.exe play checkpoints/final/policy.bin
```
The bot reads game state from memory and sends click inputs. Run as Administrator.

### Evaluate Model
```powershell
.\Release\GDLearnCPP.exe eval checkpoints/final/policy.bin
```

## Training Modes

### `genetic` — Evolutionary Algorithm
- Population of 1000 neural nets
- Evaluated in parallel on the simulator
- Top 5% survive, rest are mutations/crossovers
- **Extremely fast**: 100+ generations/second
- Best for initial exploration

### `ppo` — Proximal Policy Optimization
- Gradient-based optimization
- Better at fine-tuning precise timing
- Uses GAE (Generalized Advantage Estimation)
- Mini-batch training with Adam optimizer

### `hybrid` (recommended)
- Phase 1: Genetic warmup (500 generations) — fast exploration
- Phase 2: PPO fine-tuning — precise optimization
- Best of both worlds

## Project Structure
```
G:\gd-ml-bot\
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── src/
│   ├── config.h            # All constants, memory offsets, hyperparameters
│   ├── level_parser.h/cpp  # Parse GD level files (base64+gzip)
│   ├── simulator.h/cpp     # GD physics engine (runs 1000x+ speed)
│   ├── environment.h/cpp   # RL environment (obs, actions, rewards)
│   ├── rewards.h/cpp       # Modular reward system (GigaLearn-style)
│   ├── neural_net.h/cpp    # Standalone C++ neural network
│   ├── ppo_agent.h/cpp     # PPO training algorithm
│   ├── memory_reader.h/cpp # Read GD process memory (live mode)
│   ├── input_injector.h/cpp# Send inputs to GD window (live mode)
│   ├── trainer.h/cpp       # Training orchestrator
│   ├── logger.h/cpp        # CSV + console metrics logging
│   └── main.cpp            # Entry point + CLI
├── checkpoints/            # Saved model weights
└── logs/                   # Training metrics (CSV)
```

## Memory Offsets
The memory offsets in `config.h` are for **GD 2.206 (Build 21578706)** on Windows.
If you have a different version, update the offsets using Cheat Engine:
1. Find `GameManager` base pointer
2. Follow pointer chain to `PlayLayer` → `Player1`
3. Read player position, speed, state flags

## Tips for Best Results
- Start with `--mode genetic --levels test` to verify everything works
- For real levels, use `--mode hybrid --steps 50000000` (more steps = better)
- The bot learns faster on simpler levels first, then graduates to harder ones
- Increase population size for genetic mode if you have RAM (1000→5000)
- Monitor `logs/training_log.csv` to track progress
