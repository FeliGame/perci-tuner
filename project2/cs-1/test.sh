pass_count=0
total=10

cd cs1test/hartman/
sh ../../search.sh hartman.c 1e-4 6 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../hartman_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf " hartman:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

cd ../pendulum/
sh ../../search.sh pendulum.c 1e-4 6 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../pendulum_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf "\n pendulum:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

cd ../instantaneous_current/
sh ../../search.sh instantaneous_current.c 1e-4 6 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../instantaneous_current_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf "\n instantaneous_current:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

cd ../bessel_j1/
sh ../../search.sh bessel_j1.c 1e-4 6 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../bessel_j1_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf "\n bessel_j1:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

cd ../azimuth/
sh ../../search.sh azimuth.c 1e-4 6 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../azimuth_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf "\n azimuth:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

cd ../simpsons/
sh ../../search.sh simpsons.c 1e-4 5 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../simpsons_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf "\n simpsons:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

cd ../funarc/
sh ../../search.sh funarc.c 1e-4 3 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../funarc_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf "\n funarc:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

cd ../ellint/
sh ../../search.sh ellint.c 1e-4 6 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../ellint_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf "\n ellint:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

cd ../sum/
sh ../../search.sh sum.c 1e-4 6 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../sum_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf "\n sum:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

cd ../rocket_trajectory/
sh ../../search.sh rocket_trajectory.c 1e-4 6 30
rm temp_*
a1=$(tr -d '%' < Acc_rate.txt | xargs)
a2=$(tr -d '%' < ../rocket_trajectory_base/Acc_rate.txt | xargs)
diff=$(echo "$a1 - $a2" | bc -l)
abs_diff=$(echo "${diff#-}")
if (( $(echo "$abs_diff <= 5" | bc -l) )); then
    pass_count=$((pass_count + 1))
fi
printf "\n rocket_trajectory:\n|%.2f%% - %.2f%%| = %.2f%%\n" "$a2" "$a1" "$abs_diff" >> ../result.txt

ratio=$(echo "scale=2; $pass_count / $total * 100" | bc)
echo -e "\n通过：$pass_count 个，总数：$total 个。通过比率：$pass_count/$total = $ratio%" >> ../result.txt