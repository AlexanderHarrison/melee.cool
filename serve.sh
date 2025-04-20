#!/bin/bash
python -m http.server -d build &
xdg-open "http://0.0.0.0:8000/"
