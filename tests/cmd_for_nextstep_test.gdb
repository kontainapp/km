br main
continue

# We should be at the point where we call next_over_this_function()
# So, we perform a next command and later in the bats script ensure we "next"ed over the function
next
next

# We should be at the point where we call step_into_this_function()
# So, we perform s step command and later in the bats script ensure we "step"ed into the function.
step
step

# All done
continue
