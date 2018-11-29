# recovermjpeg
Recover JPEG pictures from broken MJPEG file

# What is recovermjpeg

_recovermjpeg_ is a small tool to recovers the images from broken MJPEG movies. Indeed if the index of the images is broken, most of the move players won't be able to read it, nor to convert it. 

![](https://github.com/cseyve/recovermjpeg/blob/master/doc/screenshots/recovermjpeg-screenshot.png?raw=true)

### Also handy on non-broken files

It can also be used on non-broken movies to extract the original JPEG from the MJPEG encapsulation, so that the separate JPEG files can be batch-processed in photography tools such as Adobe Lightroom, DxO PhotoLab, PhaseOne CaptureOne ... It is handy for improving the quality of a time-lapse with photographers' usual tools, not requiring advanced (and expensive) video tools.

### Licence

_recovermjpeg_ is released on GNU GPL v3.

### Dependencies & platforms

_recovermjpeg_ uses only Qt 5 and standard C++ library. It is portable on Linux/Windows/MacOS platforms. 

### Recommanded additional tools

_Mencoder_ and _FFmpeg_ are recommanded to convert the extracted JPEG files into movies. 
