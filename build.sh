
#!/bin/bash

rm -r build
cp -r src/ build/

cd src
for i in $(find . -name \*.html); do
    echo $i
    m4 -I ../include "$i" > "../build/$i"
done
cd ..
