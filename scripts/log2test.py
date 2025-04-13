# Verify that we have a proper apprximation of log 2 for numbers
# in range 1024..2047, representing x=1..2
import math



def approx1(i, steps, logs):
    deriv = 1477
    l = 0
    i0 = i
    i += 1024
    for j in range(0, len(steps)):
        if i >= steps[j]:
            l += logs[j]
            i = int(i*1024/steps[j])
    l += int((i-1024)*deriv/1024)
    #print(str(i0) + ": " + str(l) + ".")
    return (l/1024)

# compute the steps

previous_step = 2048
steps = [1449, 1218, 1117, 1070, 1047 ]
logs  = [512, 256, 128, 64, 32 ]
for k in range(0, len(steps)):
    if steps[k] == 0:
        x = int(math.sqrt(previous_step/1024)*1024) + 1
        steps[k] = x
        logs[k] = int(math.log2(x/1024)*1024)
        previous_step = x
    else:
        previous_step = steps[k]

# test_values = [ 512, 256, 128, 64, 32 , 1]
for k in range(0,32):
    i = 15 + k*32 
    x = i/1024
    l = math.log2(x + 1)
    lapp = approx1(i, steps, logs)
    err = lapp -l
    print(str(i) + " (" + str(x) + "): " + str(l) + " ~= " + str(lapp) + ", err: " + str(err))

max_err = 0
min_err = 0
previous_log = 0
for i in range(1,1024):
    x = i/1024
    l = math.log2(x + 1)
    lapp = approx1(i, steps, logs)
    err = lapp - l
    if lapp < previous_log:
        print("ERROR: log(1024+" + str(i) + "/1024) = " + str(lapp) + " < " + str(previous_log))
    previous_log = lapp
    if err < min_err:
        min_err = err
        print(str(i) + " (" + str(x) + "): " + str(l) + " ~= " + str(lapp) + ", err: " + str(err))
    if err > max_err:
        max_err = err
        print(str(i) + " (" + str(x) + "): " + str(l) + " ~= " + str(lapp) + ", err: " + str(err))


        
print("err: " + str(min_err) + " ... " + str(max_err))
for k in range(0, len(steps)):
    print(str(steps[k]) + ", " + str(logs[k]))
    print("alt: " + str(steps[k]-1) + ", " + str(int(math.log2((steps[k]-1)/1024)*1024)))
print(int(1024/math.log(2)))

