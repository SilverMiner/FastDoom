cd fastdoom
wmake fdoomtxt.exe EXTERNOPT=/dMODE_T8086 %1 %2 %3 %4 %5 %6 %7 %8 %9
copy fdoomtxt.exe ..\fdoomt86.exe
cd ..
sb -r fdoomt86.exe
ss fdoomt86.exe dos32a.d32