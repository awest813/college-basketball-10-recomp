// ncaabb10 - ReXGlue Recompiled Project

#include <rex/app/title_app.h>

#include "generated/default/ncaabb10_init.h"

class Ncaabb10App : public rex::app::TitleApp {
 public:
  Ncaabb10App() : TitleApp("ncaabb10", kPPCImageConfig) {}
};

REX_TITLE_APP(Ncaabb10App)
