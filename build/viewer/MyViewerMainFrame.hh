#pragma once

// standard library includes
#include <memory>
#include <string>
#include <vector>

// ROOT includes
#include "RQ_OBJECT.h"

// recoANNIE includes
#include "annie/RawReader.hh"
#include "annie/RawReadout.hh"

class TGMainFrame;
class TGraph;
class TGWindow;
class TRootEmbeddedCanvas;

class MyViewerMainFrame {

  public:
    MyViewerMainFrame(const TGWindow* p, unsigned int width,
      unsigned int height, const std::vector<std::string>& input_files);
    virtual ~MyViewerMainFrame();
    void DoDraw();

  protected:

    annie::RawReader reader_;
    std::unique_ptr<annie::RawReadout> raw_readout_ = nullptr;

    std::unique_ptr<TGraph> graph_;

    // GUI elements
    std::unique_ptr<TGMainFrame> main_frame_;
    TRootEmbeddedCanvas* canvas_;

  RQ_OBJECT("MyViewerMainFrame")
};
