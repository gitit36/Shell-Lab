import os


os.system("make test01 > testing.txt")
os.system("make test02 >> testing.txt")
os.system("make test03 >> testing.txt")
os.system("make test04 >> testing.txt")
os.system("make test05 >> testing.txt")
os.system("make test06 >> testing.txt")
os.system("make test07 >> testing.txt")
os.system("make test08 >> testing.txt")
os.system("make test09 >> testing.txt")
os.system("make test10 >> testing.txt")
os.system("make test11 >> testing.txt")
os.system("make test12 >> testing.txt")
os.system("make test13 >> testing.txt")
os.system("make test14 >> testing.txt")
os.system("make test15 >> testing.txt")
os.system("make test16 >> testing.txt")


os.system("vimdiff testing.txt correctanswer.txt")
