#include "console.h"

namespace
{
class TeePrint : public Print
{
public:
  Print *mirror = nullptr;
  size_t write(uint8_t c) override
  {
    Serial.write(c);
    if (mirror)
      mirror->write(c);
    return 1;
  }
  size_t write(const uint8_t *buf, size_t n) override
  {
    Serial.write(buf, n);
    if (mirror)
      mirror->write(buf, n);
    return n;
  }
};

TeePrint tee;
bool (*mirrorInputPending)() = nullptr;
} // namespace

Print &Console = tee;

void consoleSetMirror(Print *mirror, bool (*inputPending)())
{
  tee.mirror = mirror;
  mirrorInputPending = inputPending;
}

bool consoleAbortRequested()
{
  // Se ignoran los saltos de línea que algunos monitores envían tras la tecla
  bool pressed = false;
  while (Serial.available())
  {
    char c = Serial.read();
    if (c != '\r' && c != '\n')
      pressed = true;
  }
  return pressed || (mirrorInputPending && mirrorInputPending());
}
