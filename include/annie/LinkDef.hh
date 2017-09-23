// Ensure that ROOT (via rootcint) generates all of the dictionary entries
// needed to write recoANNIE output classes to a ROOT file
#ifdef __MAKECINT__
#pragma link C++ nestedclasses;
#pragma link C++ nestedtypedefs;
#pragma link C++ class annie::RecoPulse+;
#endif
