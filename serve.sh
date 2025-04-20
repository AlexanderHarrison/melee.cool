#!/bin/bash
python -m http.server &
xdg-open "http://0.0.0.0:8000/"
