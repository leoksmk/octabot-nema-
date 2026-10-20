"""
Prepara o framework Arduino + Bluepad32 para o PlatformIO (rode UMA vez antes do primeiro build).

Por que existe: o pacote oficial da Bluepad32 para ESP32 se chama
"framework-arduinoespressif32bluepad32" e o PlatformIO so aceita substituir o framework
Arduino por um pacote chamado "framework-arduinoespressif32". Este script baixa o pacote,
extrai em .bluepad32/framework e corrige o nome. O platformio.ini aponta para essa pasta.

Uso (na pasta deste projeto):   python preparar_bluepad32.py
No VS Code: menu do PlatformIO > "New Terminal", e rode o comando acima.
"""
import json
import os
import shutil
import sys
import urllib.request
import zipfile

VERSAO = "4.1.0"
URL = ("https://github.com/ricardoquesada/esp32-arduino-lib-builder/releases/download/"
       f"{VERSAO}/esp32-bluepad32-{VERSAO}.zip")
AQUI = os.path.dirname(os.path.abspath(__file__))
PASTA = os.path.join(AQUI, ".bluepad32")
DESTINO = os.path.join(PASTA, "framework")
ZIP = os.path.join(PASTA, f"esp32-bluepad32-{VERSAO}.zip")
NOME_PIO = "framework-arduinoespressif32"


def pronto():
    try:
        with open(os.path.join(DESTINO, "package.json"), encoding="utf-8") as f:
            pj = json.load(f)
        return pj.get("name") == NOME_PIO and pj.get("bluepad32_release") == VERSAO
    except (OSError, ValueError):
        return False


def baixar():
    print(f"Baixando Bluepad32 {VERSAO} (~200 MB, so na primeira vez)...")
    tmp = ZIP + ".parcial"
    with urllib.request.urlopen(URL) as r, open(tmp, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        lido = 0
        while True:
            bloco = r.read(1 << 20)
            if not bloco:
                break
            f.write(bloco)
            lido += len(bloco)
            if total:
                print(f"\r  {lido * 100 // total}%", end="", flush=True)
    print()
    os.replace(tmp, ZIP)


def main():
    if pronto():
        print(f"Bluepad32 {VERSAO} ja esta pronto em {DESTINO}")
        return
    os.makedirs(PASTA, exist_ok=True)
    if not os.path.isfile(ZIP):
        baixar()
    print("Extraindo...")
    shutil.rmtree(DESTINO, ignore_errors=True)
    tmp = DESTINO + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    with zipfile.ZipFile(ZIP) as z:
        z.extractall(tmp)
    raiz = os.path.join(tmp, os.listdir(tmp)[0]) if len(os.listdir(tmp)) == 1 else tmp
    os.replace(raiz, DESTINO)
    shutil.rmtree(tmp, ignore_errors=True)

    caminho = os.path.join(DESTINO, "package.json")
    with open(caminho, encoding="utf-8") as f:
        pj = json.load(f)
    pj["name"] = NOME_PIO
    pj["bluepad32_release"] = VERSAO
    with open(caminho, "w", encoding="utf-8") as f:
        json.dump(pj, f, indent=2)
    os.remove(ZIP)   # libera ~200 MB; a pasta extraida e o que o PlatformIO usa
    print(f"Pronto: {DESTINO}\nAgora e so compilar no PlatformIO (Build).")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:  # mensagem simples para quem nao e programador
        print(f"ERRO: {e}\nConfira a internet e rode de novo.", file=sys.stderr)
        sys.exit(1)
