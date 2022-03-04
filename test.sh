./microsat.exe data/aim-50-1_6-no-1.cnf >out
./microsat.exe data/aim-50-6_0-yes1-2cnf >>out
./microsat.exe data/aim-200-1_6-no-1.cnf >>out
./microsat.exe data/aim-200-1_6-yes1-3.cnf >>out
diff out expected_out -s