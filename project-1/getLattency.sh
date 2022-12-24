for size in {1..60}
do  
    ./client amber.clear.rice.edu 18005 "$((size * 1000))" 1000;
    wait
done
