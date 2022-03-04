g++ ../microsat.cpp -O2 -o microsat
./microsat aim-50-1_6-no-1.cnf >out
./microsat aim-50-6_0-yes1-2.cnf >>out
./microsat aim-200-1_6-no-1.cnf >>out
./microsat aim-200-1_6-yes1-3.cnf >>out
diff out out0 -s