#!/bin/bash

for dot_file in `ls *.dot`
do
	png_file="${dot_file%.*}"
	png_file+=".png"

	`dot -Tpng $dot_file -o $png_file`
	`rm $dot_file`
done
