uint PPU::Line::start = 0;
uint PPU::Line::count = 0;

auto PPU::Line::flush() -> void {
  //determine Mode 7 line groups for perspective correction
  ppu.mode7LineGroups.count = 0;
  if(ppu.hdPerspective()) {
    #define isLineMode7(l) (l.io.bg1.tileMode == TileMode::Mode7 \
        && !l.io.displayDisable && (l.io.bg1.aboveEnable || l.io.bg1.belowEnable))
    bool state = false;
    uint y;
    //find the Mode 7 groups
    for(y = 0; y < Line::count; y++) {
      if(state != isLineMode7(ppu.lines[Line::start + y])) {
        state = !state;
        if(state) {
          ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count] = ppu.lines[Line::start + y].y;
        } else {
          ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] = ppu.lines[Line::start + y].y - 1;
          //the lines at the edges of Mode 7 groups may be erroneous, so start and end lines for interpolation are moved inside
          int offs = (ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] - ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count]) / 8;
          ppu.mode7LineGroups.startLerpLine[ppu.mode7LineGroups.count] = ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count] + offs;
          ppu.mode7LineGroups.endLerpLine[ppu.mode7LineGroups.count] = ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] - offs;
          ppu.mode7LineGroups.count++;
        }
      }
    }
    #undef isLineMode7
    if(state) {
      //close the last group if necessary
      ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] = ppu.lines[Line::start + y].y - 1;
      int offs = (ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] - ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count]) / 8;
      ppu.mode7LineGroups.startLerpLine[ppu.mode7LineGroups.count] = ppu.mode7LineGroups.startLine[ppu.mode7LineGroups.count] + offs;
      ppu.mode7LineGroups.endLerpLine[ppu.mode7LineGroups.count] = ppu.mode7LineGroups.endLine[ppu.mode7LineGroups.count] - offs;
      ppu.mode7LineGroups.count++;
    }
    
    //detect groups that do not have perspective
    for(int i = 0; i < ppu.mode7LineGroups.count; i++) {
      //the Mode 7 scale factors of the current line
      int a = -1;
      int b = -1;
      int c = -1;
      int d = -1;
      //the Mode 7 scale factors of the previous line
      int aPrev = -1;
      int bPrev = -1;
      int cPrev = -1;
      int dPrev = -1;
      //has a varying value been found for the factors
      bool aVar = false;
      bool bVar = false;
      bool cVar = false;
      bool dVar = false;
      //has the variation been an increase or decrease
      bool aInc = false;
      bool bInc = false;
      bool cInc = false;
      bool dInc = false;
      for(y = ppu.mode7LineGroups.startLerpLine[i]; y <= ppu.mode7LineGroups.endLerpLine[i]; y++) {
        a = ((int)((int16)(ppu.lines[y].io.mode7.a)));
        b = ((int)((int16)(ppu.lines[y].io.mode7.b)));
        c = ((int)((int16)(ppu.lines[y].io.mode7.c)));
        d = ((int)((int16)(ppu.lines[y].io.mode7.d)));
        //has the value of 'a' changed compared to the last line
        //(also is the factor larger than zero, which happens sometimes and seems to be a game specific issue, mostly at the edges ofthe screen or group)
        if(aPrev > 0 && a > 0 && a != aPrev) {
          if(!aVar) {
            //if there have been no variation yet, store that there is one and store if it is an increase or decrease
            aVar = true;
            aInc = a > aPrev;
          } else if(aInc != a > aPrev) {
            //if there has been an increase and now we have a decrease, or vice versa, set the interpolationj lines to -1
            //to deactivate perspective correction for this group and stop analyzing this group
            ppu.mode7LineGroups.startLerpLine[i] = -1;
            ppu.mode7LineGroups.endLerpLine[i] = -1;
            break;
          }
        }
        //b, c and d are handled like a
        if(bPrev > 0 && b > 0 && b != bPrev) {
          if(!bVar) {
            bVar = true;
            bInc = b > bPrev;
          } else if(bInc != b > bPrev) {
            ppu.mode7LineGroups.startLerpLine[i] = -1;
            ppu.mode7LineGroups.endLerpLine[i] = -1;
            break;
          }
        }
        if(cPrev > 0 && c > 0 && c != cPrev) {
          if(!cVar) {
            cVar = true;
            cInc = c > cPrev;
          } else if(cInc != c > cPrev) {
            ppu.mode7LineGroups.startLerpLine[i] = -1;
            ppu.mode7LineGroups.endLerpLine[i] = -1;
            break;
          }
        }
        if(dPrev > 0 && d > 0 && d != dPrev) {
          if(!dVar) {
            dVar = true;
            dInc = d > dPrev;
          } else if(dInc != d > dPrev) {
            ppu.mode7LineGroups.startLerpLine[i] = -1;
            ppu.mode7LineGroups.endLerpLine[i] = -1;
            break;
          }
        }
        aPrev = a;
        bPrev = b;
        cPrev = c;
        dPrev = d;
      }
    }
  }

  //render lines (in parallel)
  if(Line::count) {
    #pragma omp parallel for if(Line::count >= 8)
    for(uint y = 0; y < Line::count; y++) {
      ppu.lines[Line::start + y].render();
    }
    Line::start = 0;
    Line::count = 0;
  }
}

auto PPU::Line::render() -> void {
  uint y = this->y + (!ppu.latch.overscan ? 7 : 0);

  auto hd = ppu.hd();
  auto ss = ppu.ss();
  auto scale = ppu.hdScale();
  auto output = ppu.output + (!hd
  ? (y * 1024 + (ppu.interlace() && ppu.field() ? 512 : 0))
  : (y * 256 * scale * scale)
  );
  auto width = (!hd
  ? (!ppu.hires() ? 256 : 512)
  : (256 * scale * scale));

  if(io.displayDisable) {
    memory::fill<uint16>(output, width);
    return;
  }

  bool hires = io.pseudoHires || io.bgMode == 5 || io.bgMode == 6;
  auto aboveColor = cgram[0];
  auto belowColor = hires ? cgram[0] : (uint16_t)io.col.fixedColor;
  uint xa =  (hd || ss) && ppu.interlace() && ppu.field() ? 256 * scale * scale / 2 : 0;
  uint xb = !(hd || ss) ? 256 : ppu.interlace() && !ppu.field() ? 256 * scale * scale / 2 : 256 * scale * scale;
  for(uint x = xa; x < xb; x++) {
    above[x] = {Source::COL, 0, aboveColor};
    below[x] = {Source::COL, 0, belowColor};
  }

  renderBackground(io.bg1, Source::BG1);
  if(!io.extbg) renderBackground(io.bg2, Source::BG2);
  renderBackground(io.bg3, Source::BG3);
  renderBackground(io.bg4, Source::BG4);
  renderObject(io.obj);
  if(io.extbg) renderBackground(io.bg2, Source::BG2);
  renderWindow(io.col.window, io.col.window.aboveMask, windowAbove);
  renderWindow(io.col.window, io.col.window.belowMask, windowBelow);

  auto luma = ppu.lightTable[io.displayBrightness];
  uint curr = 0, prev = 0;
  if(hd) for(uint x : range(256 * scale * scale)) {
    *output++ = luma[pixel(x / scale & 255, above[x], below[x])];
  } else if(width == 256) for(uint x : range(256)) {
    *output++ = luma[pixel(x, above[x], below[x])];
  } else if(!hires) for(uint x : range(256)) {
    auto color = luma[pixel(x, above[x], below[x])];
    *output++ = color;
    *output++ = color;
  } else if(!configuration.video.blurEmulation) for(uint x : range(256)) {
    *output++ = luma[pixel(x, below[x], above[x])];
    *output++ = luma[pixel(x, above[x], below[x])];
  } else for(uint x : range(256)) {
    curr = luma[pixel(x, below[x], above[x])];
    *output++ = (prev + curr - ((prev ^ curr) & 0x0421)) >> 1;
    prev = curr;
    curr = luma[pixel(x, above[x], below[x])];
    *output++ = (prev + curr - ((prev ^ curr) & 0x0421)) >> 1;
    prev = curr;
  }
}

auto PPU::Line::pixel(uint x, Pixel above, Pixel below) const -> uint16_t {
  if(!windowAbove[x]) above.color = 0x0000;
  if(!windowBelow[x]) return above.color;
  if(!io.col.enable[above.source]) return above.color;
  if(!io.col.blendMode) return blend(above.color, io.col.fixedColor, io.col.halve && windowAbove[x]);
  return blend(above.color, below.color, io.col.halve && windowAbove[x] && below.source != Source::COL);
}

auto PPU::Line::blend(uint x, uint y, bool halve) const -> uint16_t {
  if(!io.col.mathMode) {  //add
    if(!halve) {
      uint sum = x + y;
      uint carry = (sum - ((x ^ y) & 0x0421)) & 0x8420;
      return (sum - carry) | (carry - (carry >> 5));
    } else {
      return (x + y - ((x ^ y) & 0x0421)) >> 1;
    }
  } else {  //sub
    uint diff = x - y + 0x8420;
    uint borrow = (diff - ((x ^ y) & 0x8420)) & 0x8420;
    if(!halve) {
      return   (diff - borrow) & (borrow - (borrow >> 5));
    } else {
      return (((diff - borrow) & (borrow - (borrow >> 5))) & 0x7bde) >> 1;
    }
  }
}

auto PPU::Line::directColor(uint paletteIndex, uint paletteColor) const -> uint16_t {
  //paletteIndex = bgr
  //paletteColor = BBGGGRRR
  //output       = 0 BBb00 GGGg0 RRRr0
  return (paletteColor << 2 & 0x001c) + (paletteIndex <<  1 & 0x0002)   //R
       + (paletteColor << 4 & 0x0380) + (paletteIndex <<  5 & 0x0040)   //G
       + (paletteColor << 7 & 0x6000) + (paletteIndex << 10 & 0x1000);  //B
}

auto PPU::Line::plotAbove(uint x, uint source, uint priority, uint color) -> void {
  if(ppu.hd()) return plotHD(above, x, source, priority, color, false, false);
  if(priority > above[x].priority) above[x] = {source, priority, color};
}

auto PPU::Line::plotBelow(uint x, uint source, uint priority, uint color) -> void {
  if(ppu.hd()) return plotHD(below, x, source, priority, color, false, false);
  if(priority > below[x].priority) below[x] = {source, priority, color};
}

//todo: name these variables more clearly ...
auto PPU::Line::plotHD(Pixel* pixel, uint x, uint source, uint priority, uint color, bool hires, bool subpixel) -> void {
  auto scale = ppu.hdScale();
  int xss = hires && subpixel ? scale / 2 : 0;
  int ys = ppu.interlace() && ppu.field() ? scale / 2 : 0;
  if(priority > pixel[x * scale + xss + ys * 256 * scale].priority) {
    Pixel p = {source, priority, color};
    int xsm = hires && !subpixel ? scale / 2 : scale;
    int ysm = ppu.interlace() && !ppu.field() ? scale / 2 : scale;
    for(int xs = xss; xs < xsm; xs++) {
      pixel[x * scale + xs + ys * 256 * scale] = p;
    }
    int size = sizeof(Pixel) * (xsm - xss);
    Pixel* source = &pixel[x * scale + xss + ys * 256 * scale];
    for(int yst = ys + 1; yst < ysm; yst++) {
      memcpy(&pixel[x * scale + xss + yst * 256 * scale], source, size);
    }
  }
}
