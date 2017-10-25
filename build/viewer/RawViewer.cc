// ROOT includes
#include "TCanvas.h"
#include "TF1.h"
#include "TGButton.h"
#include "TGClient.h"
#include "TGFrame.h"
#include "TGraph.h"
#include "TRootEmbeddedCanvas.h"

// viewer includes
#include "RawViewer.hh"

annie::RawViewer::RawViewer(const TGWindow* p, unsigned int width,
  unsigned int height, const std::vector<std::string>& input_files)
  : reader_(input_files), main_frame_( new TGMainFrame(p, width, height) )
{
   // Create canvas widget
   canvas_ = new TRootEmbeddedCanvas("Ecanvas", main_frame_.get(), 200, 200);
   main_frame_->AddFrame( canvas_, new TGLayoutHints(kLHintsExpandX |
     kLHintsExpandY, 10, 10, 10, 1) );

   // Create a horizontal frame widget with buttons
   TGHorizontalFrame* hframe = new TGHorizontalFrame(main_frame_.get(), 200,
     40);
   TGTextButton* draw = new TGTextButton(hframe, "&Draw");
   draw->Connect("Clicked()", "annie::RawViewer", this, "DoDraw()");
   hframe->AddFrame( draw, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4) );
   TGTextButton* exit = new TGTextButton(hframe, "&Exit",
     "gApplication->Terminate(0)");
   hframe->AddFrame(exit, new TGLayoutHints(kLHintsCenterX, 5, 5, 3, 4));
   main_frame_->AddFrame(hframe, new TGLayoutHints(kLHintsCenterX, 2, 2, 2, 2));

   // Set a name to the main frame
   main_frame_->SetWindowName("Simple Example");

   // Map all subwindows of main frame
   main_frame_->MapSubwindows();

   // Initialize the layout algorithm
   main_frame_->Resize( main_frame_->GetDefaultSize() );

   // Map main frame
   main_frame_->MapWindow();
}

void annie::RawViewer::DoDraw() {

  raw_readout_ = reader_.next();

  TCanvas* can = canvas_->GetCanvas();
  can->cd();

  std::cout << "Drawing SequenceID " << raw_readout_->sequence_id() << '\n';
  const std::vector<unsigned short>& mb_data
    = raw_readout_->card(4).channel(1).minibuffer_data(0);

  size_t num_points = mb_data.size();
  graph_.reset( new TGraph(num_points) );
  for (size_t i = 0; i < num_points; ++i) {
    graph_->SetPoint(i, i * 2 /* ns */, mb_data.at(i));
  }

  graph_->SetLineColor(kBlack);
  graph_->SetLineWidth(2);

  graph_->Draw("al");

  can->Update();
}

annie::RawViewer::~RawViewer() {
   // Clean up used widgets: frames, buttons, layout hints
   main_frame_->Cleanup();
}

//void annie::RawViewer::prepare_gui() {
//
//   // Create a main frame that we'll use to run the GUI
//   main_frame_ = new TGMainFrame(gClient->GetRoot(), 1240, 794,
//     kMainFrame | kVerticalFrame);
//   main_frame_->SetName("main_frame_");
//   main_frame_->SetWindowName("ANNIE Raw Data Viewer");
//   main_frame_->SetLayoutBroken(true);
//
//   // Create a composite frame to hold everything
//   composite_frame_ = new TGCompositeFrame(main_frame_, 1240, 648,
//     kVerticalFrame);
//   composite_frame_->SetName("composite_frame_");
//   composite_frame_->SetLayoutBroken(true);
//
//   // Text label for the PMT selection list box
//   pmt_selector_label_ = new TGLabel(composite_frame_, "PMTs");
//   pmt_selector_label_->SetTextJustify(36);
//   pmt_selector_label_->SetMargins(0, 0, 0, 0);
//   pmt_selector_label_->SetWrapLength(-1);
//   composite_frame_->AddFrame(pmt_selector_label_,
//     new TGLayoutHints(kLHintsLeft | kLHintsTop, 2, 2, 2, 2));
//   pmt_selector_label_->MoveResize(920, 8, 232, 20);
//
//   // List box used to select PMTs
//   pmt_selector_ = new TGListBox(composite_frame_, -1, kSunkenFrame);
//   pmt_selector_->SetName("pmt_selector_");
//   composite_frame_->AddFrame(pmt_selector_,
//     new TGLayoutHints(kLHintsLeft | kLHintsTop | kLHintsExpandX
//     | kLHintsExpandY, 2, 2, 2, 2));
//   pmt_selector_->MoveResize(912, 32, 240, 498);
//
//   // Set up PMT selection actions
//   // Handle keyboard movements in the list box
//   TGLBContainer* pmt_selector_container
//     = dynamic_cast<TGLBContainer*>(pmt_selector_->GetContainer());
//   if (!pmt_selector_container)
//     throw "Unable to access PMT selector container!";
//   else pmt_selector_container->Connect("CurrentChanged(TGFrame*)",
//     "annie::RawViewer", this, "handle_pmt_selection()");
//   // Handle mouse clicks in the list box
//   pmt_selector_->Connect("Selected(Int_t)", "annie::RawViewer", this,
//     "handle_pmt_selection()");
//
//   // Embedded canvas to use when plotting raw waveforms
//   embedded_canvas_ = new TRootEmbeddedCanvas(0, composite_frame_, 880, 520,
//     kSunkenFrame);
//   embedded_canvas_->SetName("embedded_canvas_");
//   Int_t embedded_canvas_window_id = embedded_canvas_->GetCanvasWindowId();
//   TCanvas* dummy_canvas = new TCanvas("dummy_canvas", 10, 10,
//     embedded_canvas_window_id);
//   embedded_canvas_->AdoptCanvas(dummy_canvas);
//   composite_frame_->AddFrame(embedded_canvas_,
//     new TGLayoutHints(kLHintsLeft | kLHintsTop, 2, 2, 2, 2));
//   embedded_canvas_->MoveResize(16, 32, 880, 520);
//
//   // Text view box that information about the selected PMT
//   text_view_ = new TGTextView(composite_frame_, 888, 80);
//   text_view_->SetName("text_view_");
//   composite_frame_->AddFrame(text_view_,
//     new TGLayoutHints(kLHintsBottom | kLHintsExpandX));
//   text_view_->MoveResize(8, 560, 888, 80);
//
//   // Set the font in the text view box to something more readable (default
//   // is too small)
//   const TGFont* font = gClient
//     ->GetFontPool()->GetFont("helvetica", 20, kFontWeightNormal,
//     kFontSlantRoman);
//   if (!font) font = gClient->GetResourcePool()->GetDefaultFont();
//   text_view_->SetFont(font->GetFontStruct());
//
//   // Label for the raw waveform canvas
//   canvas_label_ = new TGLabel(composite_frame_, "Raw Waveform");
//   canvas_label_->SetTextJustify(36);
//   canvas_label_->SetMargins(0, 0, 0, 0);
//   canvas_label_->SetWrapLength(-1);
//   composite_frame_->AddFrame(canvas_label_,
//     new TGLayoutHints(kLHintsLeft | kLHintsTop, 2, 2, 2, 2));
//   canvas_label_->MoveResize(288,8,296,24);
//
//   // "Next trigger" button
//   next_button_ = new TGTextButton(composite_frame_, "next trigger", -1,
//     TGTextButton::GetDefaultGC()(), TGTextButton::GetDefaultFontStruct(),
//     kRaisedFrame);
//   next_button_->SetTextJustify(36);
//   next_button_->SetMargins(0, 0, 0, 0);
//   next_button_->SetWrapLength(-1);
//   next_button_->Resize(99, 48);
//   composite_frame_->AddFrame(next_button_,
//     new TGLayoutHints(kLHintsLeft | kLHintsTop, 2, 2, 2, 2));
//   next_button_->MoveResize(1080, 568, 99, 48);
//
//   // Set up next trigger action
//   next_button_->Connect("Clicked()", "annie::RawViewer", this,
//     "handle_next_button()");
//
//   // "Previous trigger" button
//   previous_button_ = new TGTextButton(composite_frame_, "previous trigger",
//     -1, TGTextButton::GetDefaultGC()(), TGTextButton::GetDefaultFontStruct(),
//     kRaisedFrame);
//   previous_button_->SetTextJustify(36);
//   previous_button_->SetMargins(0, 0, 0, 0);
//   previous_button_->SetWrapLength(-1);
//   previous_button_->Resize(99, 48);
//   composite_frame_->AddFrame(previous_button_,
//     new TGLayoutHints(kLHintsLeft | kLHintsTop, 2, 2, 2, 2));
//   previous_button_->MoveResize(920, 568, 99, 48);
//
//   // Set up previous trigger action
//   previous_button_->Connect("Clicked()", "annie::RawViewer", this,
//     "handle_previous_button()");
//
//   // Add the completed composite frame to the main frame.
//   // Resize everything as needed.
//   main_frame_->AddFrame(composite_frame_, new TGLayoutHints(kLHintsNormal));
//   main_frame_->MoveResize(0, 0, 1184, 647);
//   main_frame_->SetMWMHints(kMWMDecorAll, kMWMFuncAll, kMWMInputModeless);
//   main_frame_->MapSubwindows();
//   main_frame_->MapWindow();
//}
