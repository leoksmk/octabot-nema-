#!/bin/sh
# Compila e roda a simulacao no PC (Linux/macOS/WSL; precisa de g++).
# Uso: ./rodar.sh [custo_do_BP32.update_em_us]   ex.: ./rodar.sh 300
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Imock -x c++ sim.cpp -o sim && ./sim "${1:-0}" && echo "Grafico: python3 plot.py sim.png (precisa de matplotlib)"
