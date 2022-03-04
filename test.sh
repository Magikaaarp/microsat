g++ microsat.cpp -O2 -o microsat
./microsat data/aim-50-1_6-no-1.cnf >data/out
./microsat data/aim-50-6_0-yes1-2.cnf >>data/out
./microsat data/aim-200-1_6-no-1.cnf >>data/out
./microsat data/aim-200-1_6-yes1-3.cnf >>data/out
diff data/out data/out0 -s