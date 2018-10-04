[![Build Status](https://travis-ci.com/lewtds-forks/plotter.svg?branch=master)](https://travis-ci.com/lewtds-forks/plotter)

# XY-Plotter Project

Trung Ngo and Sam Abney

## G-Code Parser

### Parser Assignment

Our G-Code parser implementation defines an Instruction class where each parsed instruction is assigned one of five types. If the instruction includes further integer parameters, such as coordinates, these are stored as data members param1 and param2.

The included makefile allows the unit test to be run with the command `make test`. The test parses sample commands of each type and checks that their assigned types and parameters, if any, match what is expected. The test then parses all commands in both sample G-code files, reconstructs them from the created Instruction objects into lines in a buffer, then checks that this buffer matches the original input file.

### G-Code Documentation

- `G1 X<decimal> Y<decimal> A<int>`: Move the cursor to position `X`, `Y`. The `A` param can be ignored. The numbers are specified only up to 2 digits after decimal point. It might be better to represent them as integer.
- `G28`: Move to origin
- `M10`: Report initial status. The controller is supposed to reply with:

    `M10 XY 380 310 0.00 0.00 A0 B0 H0 S80 U160 D90`

  in which:

    - `XY`: name?
    - `380 310`: size of the printable area
    - `0.00 0.00`: XY origin (or current cursor position?)
    - `A0 B0 H0 S80`: **not sure**
    - `U160`: `M1` param for the up command
    - `D90`: `M1` param for the down command

- `M1 <int>`: Set pen position
    - `M1 90`: Pen down
    - `M1 160`: Pen up
- `M4 <byte>`: Set laser pointer power

References:

https://reprap.org/wiki/G-code
