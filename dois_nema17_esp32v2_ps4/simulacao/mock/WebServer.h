// Mock do WebServer: o teste chama as rotas direto e le a resposta.
#pragma once
#include "WiFi.h"
#include <map>

class WebServer {
 public:
  std::map<std::string, void (*)()> rotas;
  std::map<std::string, std::string> args;
  std::string resposta, tipo; int codigo = 0; int atendimentos = 0;
  explicit WebServer(int) {}
  void on(const char* p, void (*h)()) { rotas[p] = h; }
  void begin() {}
  void handleClient() { atendimentos++; }
  String arg(const char* n) { return String(args.count(n) ? args[n].c_str() : ""); }
  void send(int c, const char* t, const String& s) { codigo = c; tipo = t; resposta = s; }
  void send_P(int c, const char* t, const char* s) { codigo = c; tipo = t; resposta = s; }
  // usado pelo teste: GET p?k=v
  std::string get(const std::string& p, const std::string& k = "", const std::string& v = "") {
    args.clear(); if (!k.empty()) args[k] = v; rotas.at(p)(); return resposta;
  }
};
