all:
	vagrant up
	vagrant ssh-config > ssh-config
	scp -F ssh-config default:'tensorflow*whl' .
	vagrant halt

