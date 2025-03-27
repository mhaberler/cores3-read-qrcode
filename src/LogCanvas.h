

class LogCanvas : public M5Canvas {
  private:
    int32_t _x;
    int32_t _y;
    int32_t _w;
    int32_t _h;
  public:
    LogCanvas(LovyanGFX* parent) : M5Canvas(parent) {
    }
    void resize(int32_t x, int32_t y, int32_t w, int32_t h)  {
        _x = x;
        _y = y;
        _w = w;
        _h = h;
        deleteSprite();
        setColorDepth(8);
        setTextColor(TFT_WHITE);
        createSprite(_w, _h);
        setFont(&fonts::FreeSans9pt7b);
        setTextScroll(true);
        setCursor(0, 0);
        pushSprite(_x, _y);
    }
    size_t printf(const char *format, ...) {
        va_list arg;
        va_start(arg, format);
        size_t ret = M5Canvas::vprintf(format, arg);
        va_end(arg);
        pushSprite(_x, _y);
        return ret;
    }
};


