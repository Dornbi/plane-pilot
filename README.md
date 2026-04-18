# Plane Pilot

Plane Pilot is a 3D flight simulator prototype for the C64.

![Plane Pilot Screenshot](screens/screen01_crt.png)

## History and motivation

Plane Pilot is an attempt to show how modern compilers and AI tools can
be used in to create something retro in the late 2020s. It is also an attempt
to create a slightly different type of C64 game and see how far it can be pushed
for 3D simulations - something the C64 was absolutely not designed to do.

## Features

What you can do in Plane Pilot:

- Fly around in the world
- Render the horizon with gradients
- Basic 3D feel by rendering moving dots on the ground
- Dashboard and basic instrument panel
- Basic flight model: speed, altitude, movement, roll, pitch, yaw, stall.
- Keyboard controls
- Maintain around 10 frames per second

What you cannot do:

- There are no goals or opponents
- No objects beyond the dots - no runway, no takeoff, no landing
- No joystick support
- No sound

## How to play

TODO: Upload prg to csdb

## Controls

To fly the plane you can use the following keys:

| Keys    | Action                            |
| ------- | --------------------------------- |
| I J K L | Roll and pitch                    |
| A S     | Yaw                               |
| + -     | Throttle up and down              |
| D       | Toggle debug info                 |
| R       | Reset to starting state           |
| T       | Reset to alternate starting state |
| F       | Reset to max fuel                 |

## Development

Much of the code was done using Antigravity and Gemini. Prototyping and
data generation is in Python, and the C64 code is in C with some assembly.
To compile the code, you need the oscar64 cross-compiler.

## Inspirations

- **Stunt Car Racer** is one of the best 3D games for C64, released in 1989: https://www.youtube.com/watch?v=KMgjmIW8fd8
- **Spitfire 40** is an earlier flight sim from 1985: https://www.youtube.com/watch?v=cpq0VzBINno
- **Chuck Yeager's Air Combat** is a much more advanced flight sim for DOS/PC from 1991, served as an inspiration for the horizon rendering: https://www.youtube.com/watch?v=L1x7229289w
