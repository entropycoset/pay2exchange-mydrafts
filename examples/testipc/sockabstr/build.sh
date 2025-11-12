# Compile server
g++ -std=c++17 -Wall -O2 myapp.cpp -o myapp -lpthread

# Compile client
g++ -std=c++17 -Wall -O2 tester.cpp -o tester -lpthread

